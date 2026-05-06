#include "walkingpad_app.h"
#include "protocol/walkingpad_ftms.h"
#include "protocol/walkingpad_proto.h"
#include "protocol/walkingpad_protocol.h"
#include "views/walkingpad_main_view.h"
#include <gui/gui.h>

#define RX_STREAM_BUF_SIZE (512)

typedef enum {
  WorkerEvtStop = (1 << 0),
  WorkerEvtRxData = (1 << 1),
} WorkerEvt;

/* ── BLE callbacks ──────────────────────────────────────────────────── */

static void walkingpad_ble_state_cb(WalkingPadBleState state, void *context) {
  WalkingPadApp *app = context;
  furi_mutex_acquire(app->mutex, FuriWaitForever);
  app->ble_state = state;

  if (state == WalkingPadBleStateReady) {
    app->active_proto = walkingpad_ble_get_protocol(app->ble);

    /* Set speed limits based on protocol */
    if (app->active_proto == WalkingPadProtoFTMS) {
      WalkingPadFtmsSpeedRange range;
      if (walkingpad_ble_get_speed_range(app->ble, &range)) {
        app->min_speed_tenths = range.min_speed_001 / 10;
        app->max_speed_tenths = range.max_speed_001 / 10;
        app->speed_step_tenths = range.increment_001 / 10;
        if (app->speed_step_tenths == 0)
          app->speed_step_tenths = 5;
      } else {
        /* FTMS but no speed range yet — use safe defaults */
        app->min_speed_tenths = 10; /* 1.0 km/h */
        app->max_speed_tenths = 60;
        app->speed_step_tenths = 1; /* 0.1 km/h */
      }
    } else {
      app->min_speed_tenths = WALKINGPAD_DEFAULT_MIN_SPEED_TENTHS;
      app->max_speed_tenths = WALKINGPAD_DEFAULT_MAX_SPEED_TENTHS;
      app->speed_step_tenths = WALKINGPAD_DEFAULT_SPEED_STEP;
    }
  } else if (state != WalkingPadBleStateReady) {
    app->running = false;
  }

  furi_mutex_release(app->mutex);
  view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

static void walkingpad_ble_data_cb(WalkingPadBleDataSource source,
                                   const uint8_t *data, size_t len,
                                   void *context) {
  WalkingPadApp *app = context;

  if (source == WalkingPadBleDataFtmsTreadmill) {
    /* FTMS: decode directly — notifications are complete characteristic values
     */
    WalkingPadStatus status;
    if (ftms_decode_treadmill_data(data, len, &status)) {
      furi_mutex_acquire(app->mutex, FuriWaitForever);
      app->status = status;
      furi_mutex_release(app->mutex);
      view_dispatcher_send_custom_event(app->view_dispatcher, 0);
    }
    return;
  }

  if (source == WalkingPadBleDataFtmsStatus ||
      source == WalkingPadBleDataFtmsControl) {
    /* Machine status events — could track belt state, for now just log */
    return;
  }

  if (source == WalkingPadBleDataLegacy) {
    /* Legacy: push bytes to stream for frame assembly by worker thread */
    furi_stream_buffer_send(app->rx_stream, data, len, 0);
    furi_thread_flags_set(furi_thread_get_id(app->worker_thread),
                          WorkerEvtRxData);
  }
}

/* ── Worker thread — parses legacy protocol frames ──────────────────── */

static int32_t walkingpad_worker_thread(void *context) {
  WalkingPadApp *app = context;
  uint8_t frame[32];
  size_t frame_pos = 0;

  while (true) {
    uint32_t flags = furi_thread_flags_wait(WorkerEvtStop | WorkerEvtRxData,
                                            FuriFlagWaitAny, FuriWaitForever);
    furi_check((flags & FuriFlagError) == 0);
    if (flags & WorkerEvtStop)
      break;

    if (flags & WorkerEvtRxData) {
      while (true) {
        uint8_t byte;
        size_t rc = furi_stream_buffer_receive(app->rx_stream, &byte, 1, 0);
        if (rc == 0)
          break;

        if (frame_pos < sizeof(frame)) {
          frame[frame_pos++] = byte;

          if (frame_pos >= 2 && frame[0] == 0xF8 && frame[1] == 0xA2) {
            if (frame_pos >= 14) {
              WalkingPadStatus status;
              if (walkingpad_decode_status(frame, frame_pos, &status)) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->status = status;
                furi_mutex_release(app->mutex);
                view_dispatcher_send_custom_event(app->view_dispatcher, 0);
              }
              frame_pos = 0;
            }
          } else if (frame_pos >= 2 && frame[0] != 0xF8) {
            memmove(frame, frame + 1, frame_pos - 1);
            frame_pos--;
          } else if (frame_pos >= 32) {
            frame_pos = 0;
          }
        }
      }
    }
  }
  return 0;
}

/* ── Command helpers ────────────────────────────────────────────────── */

static void walkingpad_send_raw(WalkingPadApp *app, const uint8_t *data,
                                size_t len) {
  walkingpad_ble_send(app->ble, data, len);
}

static void walkingpad_set_speed(WalkingPadApp *app, float speed) {
  uint8_t buf[8];
  size_t n = walkingpad_proto_encode_set_speed(app->active_proto, buf,
                                               sizeof(buf), speed);
  if (n)
    walkingpad_send_raw(app, buf, n);
}

static void walkingpad_start_belt(WalkingPadApp *app) {
  uint8_t buf[8];
  size_t n = walkingpad_proto_encode_start(app->active_proto, buf, sizeof(buf));
  if (n)
    walkingpad_send_raw(app, buf, n);
}

static void walkingpad_stop_belt(WalkingPadApp *app) {
  uint8_t buf[8];
  size_t n = walkingpad_proto_encode_stop(app->active_proto, buf, sizeof(buf));
  if (n)
    walkingpad_send_raw(app, buf, n);
}

static void walkingpad_set_mode(WalkingPadApp *app, WalkingPadMode mode) {
  uint8_t buf[8];
  size_t n = walkingpad_proto_encode_set_mode(app->active_proto, buf,
                                              sizeof(buf), mode);
  if (n)
    walkingpad_send_raw(app, buf, n);
}

static void walkingpad_ask_stats(WalkingPadApp *app) {
  uint8_t buf[8];
  size_t n =
      walkingpad_proto_encode_ask_stats(app->active_proto, buf, sizeof(buf));
  if (n)
    walkingpad_send_raw(app, buf, n);
}

/* ── View callback ──────────────────────────────────────────────────── */

static void walkingpad_main_view_cb(WalkingPadMainViewInputResult result,
                                    void *context) {
  WalkingPadApp *app = context;
  furi_mutex_acquire(app->mutex, FuriWaitForever);

  switch (result) {
  case WalkingPadMainViewInputResultConnect:
    furi_mutex_release(app->mutex);
    walkingpad_ble_start(app->ble);
    return;

  case WalkingPadMainViewInputResultDisconnect:
    app->running = false;
    furi_mutex_release(app->mutex);
    walkingpad_ble_stop(app->ble);
    return;

  case WalkingPadMainViewInputResultStart:
    walkingpad_start_belt(app);
    app->running = true;
    break;

  case WalkingPadMainViewInputResultStop:
    walkingpad_stop_belt(app);
    app->running = false;
    break;

  case WalkingPadMainViewInputResultSpeedUp: {
    app->target_speed_tenths += app->speed_step_tenths;
    if (app->target_speed_tenths > app->max_speed_tenths)
      app->target_speed_tenths = app->max_speed_tenths;
    walkingpad_set_speed(app, app->target_speed_tenths / 10.0f);
    break;
  }
  case WalkingPadMainViewInputResultSpeedDown: {
    if (app->target_speed_tenths > app->speed_step_tenths)
      app->target_speed_tenths -= app->speed_step_tenths;
    else
      app->target_speed_tenths = 0;
    walkingpad_set_speed(app, app->target_speed_tenths / 10.0f);
    break;
  }
  case WalkingPadMainViewInputResultToggleMode: {
    if (!walkingpad_proto_supports_mode(app->active_proto))
      break;
    WalkingPadMode next = WalkingPadModeManual;
    if (app->status.mode == WalkingPadModeManual)
      next = WalkingPadModeAuto;
    walkingpad_set_mode(app, next);
    break;
  }
  default:
    break;
  }

  WalkingPadStatus st = app->status;
  WalkingPadBleState ble_st = app->ble_state;
  WalkingPadProtoType proto = app->active_proto;
  uint8_t tgt = app->target_speed_tenths;
  furi_mutex_release(app->mutex);

  walkingpad_main_view_update_state(app->main_view, ble_st, proto, &st, tgt);
}

/* ── Navigation / tick / custom event callbacks ─────────────────────── */

static uint32_t walkingpad_exit(void *context) {
  UNUSED(context);
  return VIEW_NONE;
}

static void walkingpad_tick_event(void *context) {
  WalkingPadApp *app = context;
  static uint8_t tick_count = 0;
  tick_count++;
  if (tick_count >= 3) {
    tick_count = 0;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    WalkingPadBleState st = app->ble_state;
    WalkingPadProtoType proto = app->active_proto;
    furi_mutex_release(app->mutex);

    /* Only poll stats for legacy — FTMS pushes data via notifications */
    if (st == WalkingPadBleStateReady && proto == WalkingPadProtoLegacy) {
      walkingpad_ask_stats(app);
    }
  }
}

static bool walkingpad_custom_event_callback(void *context, uint32_t event) {
  UNUSED(event);
  WalkingPadApp *app = context;
  WalkingPadStatus st;
  WalkingPadBleState ble_st;
  WalkingPadProtoType proto;
  uint8_t tgt;
  furi_mutex_acquire(app->mutex, FuriWaitForever);
  st = app->status;
  ble_st = app->ble_state;
  proto = app->active_proto;
  tgt = app->target_speed_tenths;
  furi_mutex_release(app->mutex);
  walkingpad_main_view_update_state(app->main_view, ble_st, proto, &st, tgt);
  return true;
}

/* ── App lifecycle ──────────────────────────────────────────────────── */

static WalkingPadApp *walkingpad_app_alloc() {
  WalkingPadApp *app = malloc(sizeof(WalkingPadApp));
  memset(app, 0, sizeof(WalkingPadApp));

  app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
  app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_BUF_SIZE, 1);

  app->gui = furi_record_open(RECORD_GUI);
  app->view_dispatcher = view_dispatcher_alloc();
  view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui,
                                ViewDispatcherTypeFullscreen);

  /* BLE transport */
  app->ble = walkingpad_ble_alloc();
  walkingpad_ble_set_state_callback(app->ble, walkingpad_ble_state_cb, app);
  walkingpad_ble_set_data_callback(app->ble, walkingpad_ble_data_cb, app);

  /* Worker thread for legacy frame assembly */
  app->worker_thread = furi_thread_alloc_ex("WalkingPadWorker", 1024,
                                            walkingpad_worker_thread, app);
  furi_thread_start(app->worker_thread);

  /* Default speed config */
  app->target_speed_tenths = 25;
  app->min_speed_tenths = WALKINGPAD_DEFAULT_MIN_SPEED_TENTHS;
  app->max_speed_tenths = WALKINGPAD_DEFAULT_MAX_SPEED_TENTHS;
  app->speed_step_tenths = WALKINGPAD_DEFAULT_SPEED_STEP;

  return app;
}

static void walkingpad_app_free(WalkingPadApp *app) {
  furi_thread_flags_set(furi_thread_get_id(app->worker_thread), WorkerEvtStop);
  furi_thread_join(app->worker_thread);
  furi_thread_free(app->worker_thread);

  walkingpad_ble_free(app->ble);

  view_dispatcher_free(app->view_dispatcher);
  furi_record_close(RECORD_GUI);

  furi_stream_buffer_free(app->rx_stream);
  furi_mutex_free(app->mutex);
  free(app);
}

int32_t walkingpad_app(void *p) {
  UNUSED(p);

  WalkingPadApp *app = walkingpad_app_alloc();

  app->main_view = walkingpad_main_view_alloc();
  walkingpad_main_view_set_callback(app->main_view, walkingpad_main_view_cb,
                                    app);

  view_dispatcher_set_custom_event_callback(app->view_dispatcher,
                                            walkingpad_custom_event_callback);
  view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
  view_dispatcher_set_tick_event_callback(app->view_dispatcher,
                                          walkingpad_tick_event, 1000);

  view_set_previous_callback(walkingpad_main_view_get_view(app->main_view),
                             walkingpad_exit);
  view_dispatcher_add_view(app->view_dispatcher, 0,
                           walkingpad_main_view_get_view(app->main_view));
  view_dispatcher_switch_to_view(app->view_dispatcher, 0);

  view_dispatcher_run(app->view_dispatcher);

  view_dispatcher_remove_view(app->view_dispatcher, 0);
  walkingpad_main_view_free(app->main_view);
  walkingpad_app_free(app);

  return 0;
}
