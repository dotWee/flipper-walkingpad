#pragma once

#include "../protocol/walkingpad_ftms.h"
#include "../protocol/walkingpad_protocol.h"
#include <furi.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Native BLE transport for WalkingPad (legacy WiLink + FTMS).
 *
 * Scans for devices advertising either service UUID 0xFE00 (legacy)
 * or 0x1826 (FTMS).  Auto-detects the protocol based on which
 * service is found and configures the connection accordingly.
 */

typedef enum {
  WalkingPadBleStateIdle,
  WalkingPadBleStateScanning,
  WalkingPadBleStateConnecting,
  WalkingPadBleStateDiscovering,
  WalkingPadBleStateReady,
  WalkingPadBleStateError,
} WalkingPadBleState;

/** Identifies which characteristic a notification came from. */
typedef enum {
  WalkingPadBleDataLegacy, /* FE02 notification (legacy raw frame bytes) */
  WalkingPadBleDataFtmsTreadmill, /* 0x2ACD Treadmill Data */
  WalkingPadBleDataFtmsStatus,    /* 0x2ADA Fitness Machine Status */
  WalkingPadBleDataFtmsControl,   /* 0x2AD9 Control Point indication */
} WalkingPadBleDataSource;

typedef void (*WalkingPadBleStateCallback)(WalkingPadBleState state,
                                           void *context);
typedef void (*WalkingPadBleDataCallback)(WalkingPadBleDataSource source,
                                          const uint8_t *data, size_t len,
                                          void *context);

typedef struct WalkingPadBle WalkingPadBle;

WalkingPadBle *walkingpad_ble_alloc(void);
void walkingpad_ble_free(WalkingPadBle *ble);

void walkingpad_ble_set_state_callback(WalkingPadBle *ble,
                                       WalkingPadBleStateCallback cb,
                                       void *context);
void walkingpad_ble_set_data_callback(WalkingPadBle *ble,
                                      WalkingPadBleDataCallback cb,
                                      void *context);

bool walkingpad_ble_start(WalkingPadBle *ble);
void walkingpad_ble_stop(WalkingPadBle *ble);

/** Send a command.  Writes to FE01 (legacy) or 0x2AD9 (FTMS). */
bool walkingpad_ble_send(WalkingPadBle *ble, const uint8_t *data, size_t len);

WalkingPadBleState walkingpad_ble_get_state(const WalkingPadBle *ble);

/** Get the auto-detected protocol type (valid after Ready). */
WalkingPadProtoType walkingpad_ble_get_protocol(const WalkingPadBle *ble);

/** Get FTMS speed range (valid after Ready, FTMS only). Returns false if
 * unavailable. */
bool walkingpad_ble_get_speed_range(const WalkingPadBle *ble,
                                    WalkingPadFtmsSpeedRange *range);
