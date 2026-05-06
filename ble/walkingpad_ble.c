/**
 * Native BLE Central transport for WalkingPad — supports both legacy
 * WiLink (0xFE00) and FTMS (0x1826) protocols.
 *
 * Protocol is auto-detected during BLE scanning based on which service
 * UUID the device advertises.
 */

#include "walkingpad_ble.h"

#include <furi.h>
#include <furi_ble/gatt_client.h>
#include <furi_hal.h>
#include <gap.h>

#define TAG "WalkingPadBLE"

/* Legacy WiLink UUIDs */
#define LEGACY_SVC_UUID_16 0xFE00
#define LEGACY_RX_UUID_16 0xFE01 /* host -> pad  (write) */
#define LEGACY_TX_UUID_16 0xFE02 /* pad -> host  (notify) */

/* Scan parameters */
#define SCAN_INTERVAL 0x0060
#define SCAN_WINDOW 0x0030
#define SCAN_TIMEOUT_MS 30000

/* Connection timing */
#define CONNECT_TIMEOUT_MS 15000
#define CONNECT_POLL_MS 100

struct WalkingPadBle {
  WalkingPadBleState state;
  WalkingPadProtoType detected_proto;
  FuriMutex *mutex;

  /* Callbacks */
  WalkingPadBleStateCallback state_cb;
  void *state_cb_ctx;
  WalkingPadBleDataCallback data_cb;
  void *data_cb_ctx;

  /* Connection */
  uint16_t conn_handle;
  uint8_t peer_addr_type;
  uint8_t peer_addr[6];

  /* Legacy characteristic handles */
  uint16_t legacy_write_handle;  /* FE01 */
  uint16_t legacy_notify_handle; /* FE02 */

  /* FTMS characteristic handles */
  uint16_t ftms_treadmill_data_handle; /* 0x2ACD notify */
  uint16_t ftms_control_point_handle;  /* 0x2AD9 write */
  uint16_t ftms_speed_range_handle;    /* 0x2AD4 read */
  uint16_t ftms_machine_status_handle; /* 0x2ADA notify */

  /* FTMS speed range (populated from 0x2AD4 read) */
  WalkingPadFtmsSpeedRange speed_range;
  bool speed_range_valid;

  /* Discovery state */
  bool svc_found;
  bool chars_found;
  bool target_found;

  /* Timers */
  FuriTimer *timeout_timer;
  FuriTimer *poll_timer;
  uint32_t connect_elapsed_ms;
};

/* ── Helpers ────────────────────────────────────────────────────────── */

static void set_state(WalkingPadBle *ble, WalkingPadBleState state) {
  furi_mutex_acquire(ble->mutex, FuriWaitForever);
  ble->state = state;
  WalkingPadBleStateCallback cb = ble->state_cb;
  void *ctx = ble->state_cb_ctx;
  furi_mutex_release(ble->mutex);
  if (cb)
    cb(state, ctx);
}

static void fire_data(WalkingPadBle *ble, WalkingPadBleDataSource src,
                      const uint8_t *data, size_t len) {
  furi_mutex_acquire(ble->mutex, FuriWaitForever);
  WalkingPadBleDataCallback cb = ble->data_cb;
  void *ctx = ble->data_cb_ctx;
  furi_mutex_release(ble->mutex);
  if (cb && len > 0)
    cb(src, data, len, ctx);
}

/**
 * Parse BLE advertisement AD structures looking for 16-bit service UUIDs.
 * Returns the detected protocol or Unknown.
 */
static WalkingPadProtoType match_ad_proto(const uint8_t *data, uint8_t len) {
  bool found_legacy = false;
  bool found_ftms = false;

  size_t i = 0;
  while (i + 1 < len) {
    uint8_t ad_len = data[i];
    if (ad_len == 0 || i + 1 + ad_len > len)
      break;
    uint8_t ad_type = data[i + 1];

    /* 0x02 / 0x03 = Incomplete / Complete list of 16-bit UUIDs */
    if (ad_type == 0x02 || ad_type == 0x03) {
      for (size_t j = i + 2; j + 1 < i + 1 + ad_len; j += 2) {
        uint16_t uuid16 = (uint16_t)data[j] | ((uint16_t)data[j + 1] << 8);
        if (uuid16 == LEGACY_SVC_UUID_16)
          found_legacy = true;
        if (uuid16 == FTMS_SVC_UUID_16)
          found_ftms = true;
      }
    }
    i += 1 + ad_len;
  }

  /* Prefer FTMS if both are present (newer protocol) */
  if (found_ftms)
    return WalkingPadProtoFTMS;
  if (found_legacy)
    return WalkingPadProtoLegacy;
  return WalkingPadProtoUnknown;
}

static void reset_handles(WalkingPadBle *ble) {
  ble->legacy_write_handle = 0;
  ble->legacy_notify_handle = 0;
  ble->ftms_treadmill_data_handle = 0;
  ble->ftms_control_point_handle = 0;
  ble->ftms_speed_range_handle = 0;
  ble->ftms_machine_status_handle = 0;
  ble->svc_found = false;
  ble->chars_found = false;
  ble->speed_range_valid = false;
}

/* ── Timeout ────────────────────────────────────────────────────────── */

static void timeout_cb(void *context) {
  WalkingPadBle *ble = context;
  FURI_LOG_W(TAG, "Timeout in state %d", ble->state);
  if (ble->state == WalkingPadBleStateScanning)
    gap_stop_scanning();
  furi_timer_stop(ble->poll_timer);
  set_state(ble, WalkingPadBleStateError);
}

/* Forward declarations */
static void gatt_cb(BleGattClientEvent *event, void *context);

/* ── Connection poll ────────────────────────────────────────────────── */

static void poll_cb(void *context) {
  WalkingPadBle *ble = context;

  if (ble->state == WalkingPadBleStateConnecting) {
    uint16_t h = gap_get_connection_handle_by_role(true);
    if (h != 0) {
      FURI_LOG_I(TAG, "Connected, handle=%d", h);
      furi_timer_stop(ble->poll_timer);
      furi_timer_stop(ble->timeout_timer);

      furi_mutex_acquire(ble->mutex, FuriWaitForever);
      ble->conn_handle = h;
      furi_mutex_release(ble->mutex);

      ble_gatt_client_set_callback(h, gatt_cb, ble);
      set_state(ble, WalkingPadBleStateDiscovering);

      ble_gatt_client_exchange_mtu(h);
      ble_gatt_client_discover_services(h);
      return;
    }

    ble->connect_elapsed_ms += CONNECT_POLL_MS;
    if (ble->connect_elapsed_ms >= CONNECT_TIMEOUT_MS) {
      FURI_LOG_W(TAG, "Connection timeout");
      furi_timer_stop(ble->poll_timer);
      furi_timer_stop(ble->timeout_timer);
      set_state(ble, WalkingPadBleStateError);
    }
  } else if (ble->state == WalkingPadBleStateReady) {
    uint16_t h = gap_get_connection_handle_by_role(true);
    if (h == 0) {
      FURI_LOG_I(TAG, "Disconnected (poll)");
      furi_timer_stop(ble->poll_timer);
      furi_mutex_acquire(ble->mutex, FuriWaitForever);
      ble->conn_handle = 0;
      reset_handles(ble);
      furi_mutex_release(ble->mutex);
      set_state(ble, WalkingPadBleStateIdle);
    }
  }
}

/* ── Scan callback ──────────────────────────────────────────────────── */

static void scan_cb(GapScanResultData *result, void *context) {
  WalkingPadBle *ble = context;
  if (ble->state != WalkingPadBleStateScanning)
    return;

  WalkingPadProtoType proto = match_ad_proto(result->data, result->data_len);
  if (proto == WalkingPadProtoUnknown)
    return;

  FURI_LOG_I(TAG, "WalkingPad found (%s): %02X:%02X:%02X:%02X:%02X:%02X",
             proto == WalkingPadProtoFTMS ? "FTMS" : "Legacy",
             result->address[5], result->address[4], result->address[3],
             result->address[2], result->address[1], result->address[0]);

  gap_stop_scanning();
  furi_timer_stop(ble->timeout_timer);

  furi_mutex_acquire(ble->mutex, FuriWaitForever);
  ble->peer_addr_type = result->address_type;
  memcpy(ble->peer_addr, result->address, 6);
  ble->detected_proto = proto;
  ble->target_found = true;
  furi_mutex_release(ble->mutex);

  set_state(ble, WalkingPadBleStateConnecting);
  ble->connect_elapsed_ms = 0;
  furi_timer_start(ble->poll_timer, CONNECT_POLL_MS);

  if (!gap_connect(ble->peer_addr_type, ble->peer_addr)) {
    FURI_LOG_E(TAG, "gap_connect failed");
    furi_timer_stop(ble->poll_timer);
    set_state(ble, WalkingPadBleStateError);
  }
}

/* ── GATT Client callback ──────────────────────────────────────────── */

static void gatt_cb(BleGattClientEvent *event, void *context) {
  WalkingPadBle *ble = context;

  switch (event->type) {
  case BleGattClientEventDiscoverComplete: {
    FURI_LOG_I(TAG, "Service discovery, count=%d",
               event->discover.service_count);

    uint16_t target_uuid = (ble->detected_proto == WalkingPadProtoFTMS)
                               ? FTMS_SVC_UUID_16
                               : LEGACY_SVC_UUID_16;

    for (uint8_t i = 0; i < event->discover.service_count; i++) {
      const BleGattService *svc = &event->discover.services[i];
      if (svc->uuid_type == BleGattUuidType16 &&
          svc->uuid.uuid16 == target_uuid) {
        FURI_LOG_I(TAG, "Target service found, handles=%d-%d",
                   svc->start_handle, svc->end_handle);
        furi_mutex_acquire(ble->mutex, FuriWaitForever);
        ble->svc_found = true;
        furi_mutex_release(ble->mutex);
        ble_gatt_client_discover_characteristics(ble->conn_handle, svc);
        return;
      }
    }
    FURI_LOG_E(TAG, "Target service 0x%04X not found", target_uuid);
    gap_disconnect(ble->conn_handle);
    set_state(ble, WalkingPadBleStateError);
    break;
  }

  case BleGattClientEventCharDiscoverComplete: {
    FURI_LOG_I(TAG, "Char discovery, count=%d",
               event->char_discover.char_count);
    furi_mutex_acquire(ble->mutex, FuriWaitForever);

    for (uint8_t i = 0; i < event->char_discover.char_count; i++) {
      const BleGattCharacteristic *chr =
          &event->char_discover.characteristics[i];
      if (chr->uuid_type != BleGattUuidType16)
        continue;

      if (ble->detected_proto == WalkingPadProtoLegacy) {
        if (chr->uuid.uuid16 == LEGACY_RX_UUID_16)
          ble->legacy_write_handle = chr->value_handle;
        else if (chr->uuid.uuid16 == LEGACY_TX_UUID_16)
          ble->legacy_notify_handle = chr->value_handle;
      } else {
        switch (chr->uuid.uuid16) {
        case FTMS_TREADMILL_DATA_UUID_16:
          ble->ftms_treadmill_data_handle = chr->value_handle;
          FURI_LOG_I(TAG, "2ACD handle=%d", chr->value_handle);
          break;
        case FTMS_CONTROL_POINT_UUID_16:
          ble->ftms_control_point_handle = chr->value_handle;
          FURI_LOG_I(TAG, "2AD9 handle=%d", chr->value_handle);
          break;
        case FTMS_SPEED_RANGE_UUID_16:
          ble->ftms_speed_range_handle = chr->value_handle;
          FURI_LOG_I(TAG, "2AD4 handle=%d", chr->value_handle);
          break;
        case FTMS_MACHINE_STATUS_UUID_16:
          ble->ftms_machine_status_handle = chr->value_handle;
          FURI_LOG_I(TAG, "2ADA handle=%d", chr->value_handle);
          break;
        default:
          break;
        }
      }
    }

    bool ok;
    if (ble->detected_proto == WalkingPadProtoLegacy) {
      ok = (ble->legacy_write_handle != 0 && ble->legacy_notify_handle != 0);
    } else {
      ok = (ble->ftms_treadmill_data_handle != 0 &&
            ble->ftms_control_point_handle != 0);
    }
    ble->chars_found = ok;
    furi_mutex_release(ble->mutex);

    if (!ok) {
      FURI_LOG_E(TAG, "Required characteristics not found");
      gap_disconnect(ble->conn_handle);
      set_state(ble, WalkingPadBleStateError);
      return;
    }

    /* Subscribe to notifications */
    if (ble->detected_proto == WalkingPadProtoLegacy) {
      ble_gatt_client_subscribe_notifications(ble->conn_handle,
                                              ble->legacy_notify_handle, true);
    } else {
      ble_gatt_client_subscribe_notifications(
          ble->conn_handle, ble->ftms_treadmill_data_handle, true);
      if (ble->ftms_machine_status_handle != 0) {
        ble_gatt_client_subscribe_notifications(
            ble->conn_handle, ble->ftms_machine_status_handle, true);
      }
      /* Read speed range */
      if (ble->ftms_speed_range_handle != 0) {
        ble_gatt_client_read(ble->conn_handle, ble->ftms_speed_range_handle);
      }
      /* Send REQUEST_CONTROL */
      uint8_t req_ctrl = FTMS_CP_REQUEST_CONTROL;
      ble_gatt_client_write(ble->conn_handle, ble->ftms_control_point_handle,
                            &req_ctrl, 1);
    }

    set_state(ble, WalkingPadBleStateReady);
    furi_timer_start(ble->poll_timer, 500); /* monitor disconnection */
    break;
  }

  case BleGattClientEventNotification: {
    uint16_t ch = event->notification.char_handle;
    WalkingPadBleDataSource src;

    if (ble->detected_proto == WalkingPadProtoLegacy) {
      src = WalkingPadBleDataLegacy;
    } else if (ch == ble->ftms_treadmill_data_handle) {
      src = WalkingPadBleDataFtmsTreadmill;
    } else if (ch == ble->ftms_machine_status_handle) {
      src = WalkingPadBleDataFtmsStatus;
    } else if (ch == ble->ftms_control_point_handle) {
      src = WalkingPadBleDataFtmsControl;
    } else {
      break; /* unknown char */
    }

    fire_data(ble, src, event->notification.data, event->notification.data_len);
    break;
  }

  case BleGattClientEventReadComplete:
    /* Speed range read response (FTMS) */
    if (ble->detected_proto == WalkingPadProtoFTMS &&
        event->read.data_len >= 6) {
      furi_mutex_acquire(ble->mutex, FuriWaitForever);
      if (ftms_decode_speed_range(event->read.data, event->read.data_len,
                                  &ble->speed_range)) {
        ble->speed_range_valid = true;
        FURI_LOG_I(TAG, "Speed range: %.2f-%.2f km/h (step %.2f)",
                   (double)(ble->speed_range.min_speed_001 / 100.0f),
                   (double)(ble->speed_range.max_speed_001 / 100.0f),
                   (double)(ble->speed_range.increment_001 / 100.0f));
      }
      furi_mutex_release(ble->mutex);
    }
    break;

  case BleGattClientEventWriteComplete:
  case BleGattClientEventMtuExchangeComplete:
    break;

  case BleGattClientEventError:
    FURI_LOG_E(TAG, "GATT error");
    break;

  default:
    break;
  }
}

/* ── Public API ─────────────────────────────────────────────────────── */

WalkingPadBle *walkingpad_ble_alloc(void) {
  WalkingPadBle *ble = malloc(sizeof(WalkingPadBle));
  memset(ble, 0, sizeof(WalkingPadBle));
  ble->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
  ble->state = WalkingPadBleStateIdle;
  ble->timeout_timer = furi_timer_alloc(timeout_cb, FuriTimerTypeOnce, ble);
  ble->poll_timer = furi_timer_alloc(poll_cb, FuriTimerTypePeriodic, ble);

  ble_gatt_client_init();
  gap_set_no_pairing();
  return ble;
}

void walkingpad_ble_free(WalkingPadBle *ble) {
  if (!ble)
    return;
  walkingpad_ble_stop(ble);
  ble_gatt_client_deinit();
  furi_timer_free(ble->poll_timer);
  furi_timer_free(ble->timeout_timer);
  furi_mutex_free(ble->mutex);
  free(ble);
}

void walkingpad_ble_set_state_callback(WalkingPadBle *ble,
                                       WalkingPadBleStateCallback cb,
                                       void *context) {
  furi_mutex_acquire(ble->mutex, FuriWaitForever);
  ble->state_cb = cb;
  ble->state_cb_ctx = context;
  furi_mutex_release(ble->mutex);
}

void walkingpad_ble_set_data_callback(WalkingPadBle *ble,
                                      WalkingPadBleDataCallback cb,
                                      void *context) {
  furi_mutex_acquire(ble->mutex, FuriWaitForever);
  ble->data_cb = cb;
  ble->data_cb_ctx = context;
  furi_mutex_release(ble->mutex);
}

bool walkingpad_ble_start(WalkingPadBle *ble) {
  if (ble->state != WalkingPadBleStateIdle &&
      ble->state != WalkingPadBleStateError) {
    FURI_LOG_W(TAG, "Already active (state=%d)", ble->state);
    return false;
  }

  furi_mutex_acquire(ble->mutex, FuriWaitForever);
  reset_handles(ble);
  ble->conn_handle = 0;
  ble->target_found = false;
  ble->detected_proto = WalkingPadProtoUnknown;
  ble->connect_elapsed_ms = 0;
  furi_mutex_release(ble->mutex);

  gap_set_scan_callback(scan_cb, ble);

  GapScanParams params = {
      .interval = SCAN_INTERVAL,
      .window = SCAN_WINDOW,
      .active = true,
      .timeout_ms = 0,
  };

  set_state(ble, WalkingPadBleStateScanning);

  if (!gap_start_scanning(&params)) {
    FURI_LOG_E(TAG, "gap_start_scanning failed");
    set_state(ble, WalkingPadBleStateError);
    return false;
  }

  furi_timer_start(ble->timeout_timer, SCAN_TIMEOUT_MS);
  FURI_LOG_I(TAG, "Scanning for WalkingPad (Legacy + FTMS)...");
  return true;
}

void walkingpad_ble_stop(WalkingPadBle *ble) {
  furi_timer_stop(ble->timeout_timer);
  furi_timer_stop(ble->poll_timer);

  if (ble->state == WalkingPadBleStateScanning) {
    gap_stop_scanning();
  }

  if (ble->conn_handle != 0) {
    /* Unsubscribe before disconnect */
    if (ble->detected_proto == WalkingPadProtoLegacy &&
        ble->legacy_notify_handle) {
      ble_gatt_client_subscribe_notifications(ble->conn_handle,
                                              ble->legacy_notify_handle, false);
    } else if (ble->detected_proto == WalkingPadProtoFTMS) {
      if (ble->ftms_treadmill_data_handle) {
        ble_gatt_client_subscribe_notifications(
            ble->conn_handle, ble->ftms_treadmill_data_handle, false);
      }
      if (ble->ftms_machine_status_handle) {
        ble_gatt_client_subscribe_notifications(
            ble->conn_handle, ble->ftms_machine_status_handle, false);
      }
    }
    ble_gatt_client_set_callback(ble->conn_handle, NULL, NULL);
    gap_disconnect(ble->conn_handle);
  }

  furi_mutex_acquire(ble->mutex, FuriWaitForever);
  ble->conn_handle = 0;
  reset_handles(ble);
  ble->target_found = false;
  furi_mutex_release(ble->mutex);

  set_state(ble, WalkingPadBleStateIdle);
}

bool walkingpad_ble_send(WalkingPadBle *ble, const uint8_t *data, size_t len) {
  if (ble->state != WalkingPadBleStateReady)
    return false;

  furi_mutex_acquire(ble->mutex, FuriWaitForever);
  uint16_t handle = ble->conn_handle;
  uint16_t target = (ble->detected_proto == WalkingPadProtoFTMS)
                        ? ble->ftms_control_point_handle
                        : ble->legacy_write_handle;
  furi_mutex_release(ble->mutex);

  if (handle == 0 || target == 0)
    return false;
  return ble_gatt_client_write(handle, target, data, (uint16_t)len);
}

WalkingPadBleState walkingpad_ble_get_state(const WalkingPadBle *ble) {
  return ble->state;
}

WalkingPadProtoType walkingpad_ble_get_protocol(const WalkingPadBle *ble) {
  return ble->detected_proto;
}

bool walkingpad_ble_get_speed_range(const WalkingPadBle *ble,
                                    WalkingPadFtmsSpeedRange *range) {
  if (!ble->speed_range_valid)
    return false;
  *range = ble->speed_range;
  return true;
}
