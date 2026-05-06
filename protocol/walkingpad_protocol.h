#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  WalkingPadModeAuto = 0,
  WalkingPadModeManual = 1,
  WalkingPadModeStandby = 2,
  WalkingPadModeCalibration = 4,
} WalkingPadMode;

typedef enum {
  WalkingPadProtoUnknown = 0,
  WalkingPadProtoLegacy, /* WiLink: service 0xFE00 (A1, A1 Pro, R1/R2 Pro) */
  WalkingPadProtoFTMS,   /* FTMS:   service 0x1826 (KS-HD-*, KS-MC21-*, etc.) */
} WalkingPadProtoType;

typedef struct {
  float speed_kmh;
  WalkingPadMode mode;
  uint32_t time_seconds;
  float distance_km;
  uint32_t steps;
  uint16_t calories; /* total kcal (FTMS only; 0 for legacy) */
} WalkingPadStatus;

/* Encode WalkingPad commands into 6-byte frames.  Returns bytes written. */
size_t walkingpad_encode_cmd_ask_stats(uint8_t *buf, size_t buf_size);
size_t walkingpad_encode_cmd_set_speed(uint8_t *buf, size_t buf_size,
                                       float speed_kmh);
size_t walkingpad_encode_cmd_set_mode(uint8_t *buf, size_t buf_size,
                                      WalkingPadMode mode);
size_t walkingpad_encode_cmd_start_belt(uint8_t *buf, size_t buf_size);
size_t walkingpad_encode_cmd_stop_belt(uint8_t *buf, size_t buf_size);

/* Decode a status response frame.  Returns true on success. */
bool walkingpad_decode_status(const uint8_t *buf, size_t len,
                              WalkingPadStatus *status);

#ifdef __cplusplus
}
#endif
