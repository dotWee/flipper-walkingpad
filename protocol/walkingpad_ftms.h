#pragma once

#include "walkingpad_protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * FTMS (Fitness Machine Service) codec for newer KingSmith WalkingPad models.
 *
 * Supported models: KS-HD-*, KS-MC21-*, KS-SMC21C-*, ZP-ZEALR1-*
 *
 * BLE characteristics:
 *   0x2ACD  Treadmill Data        (notify)  — live
 * speed/dist/time/steps/calories 0x2AD9  FTMS Control Point    (write)   —
 * commands (start/stop/speed) 0x2AD4  Supported Speed Range (read)    —
 * min/max/increment 0x2ADA  Fitness Machine Status(notify)  — belt events
 */

/* FTMS Control Point opcodes */
#define FTMS_CP_REQUEST_CONTROL 0x00
#define FTMS_CP_RESET 0x01
#define FTMS_CP_SET_TARGET_SPEED 0x02 /* + uint16 LE speed in 0.01 km/h */
#define FTMS_CP_START_RESUME 0x07
#define FTMS_CP_STOP_PAUSE 0x08 /* + uint8: 0x01=stop, 0x02=pause */

/* FTMS Control Point response */
#define FTMS_CP_RESPONSE 0x80
#define FTMS_CP_RESULT_SUCCESS 0x01

/* FTMS BLE UUIDs (16-bit short forms) */
#define FTMS_SVC_UUID_16 0x1826
#define FTMS_TREADMILL_DATA_UUID_16 0x2ACD
#define FTMS_CONTROL_POINT_UUID_16 0x2AD9
#define FTMS_SPEED_RANGE_UUID_16 0x2AD4
#define FTMS_MACHINE_STATUS_UUID_16 0x2ADA
#define FTMS_MACHINE_FEATURE_UUID_16 0x2ACC
#define FTMS_TRAINING_STATUS_UUID_16 0x2AD3

typedef struct {
  uint16_t min_speed_001; /* 0.01 km/h units */
  uint16_t max_speed_001; /* 0.01 km/h units */
  uint16_t increment_001; /* 0.01 km/h units */
} WalkingPadFtmsSpeedRange;

/* ── Encode FTMS Control Point commands ─────────────────────────────── */

size_t ftms_encode_request_control(uint8_t *buf, size_t buf_size);
size_t ftms_encode_set_speed(uint8_t *buf, size_t buf_size, float speed_kmh);
size_t ftms_encode_start(uint8_t *buf, size_t buf_size);
size_t ftms_encode_stop(uint8_t *buf, size_t buf_size);
size_t ftms_encode_reset(uint8_t *buf, size_t buf_size);

/* ── Decode FTMS notifications ──────────────────────────────────────── */

/**
 * Decode FTMS Treadmill Data (0x2ACD) notification.
 * Parses the flags bitfield and extracts speed, distance, time, steps,
 * calories. Always sets mode = WalkingPadModeManual.
 * Steps are available via KingSmith's proprietary extension at bit 13.
 */
bool ftms_decode_treadmill_data(const uint8_t *buf, size_t len,
                                WalkingPadStatus *status);

/** Decode FTMS Supported Speed Range (0x2AD4). Expects 6 bytes. */
bool ftms_decode_speed_range(const uint8_t *buf, size_t len,
                             WalkingPadFtmsSpeedRange *range);

#ifdef __cplusplus
}
#endif
