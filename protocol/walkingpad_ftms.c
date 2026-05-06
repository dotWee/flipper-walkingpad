/**
 * FTMS (Fitness Machine Service) codec for newer KingSmith WalkingPad models.
 *
 * Command encoding follows the standard FTMS Control Point format.
 * Treadmill Data decoding handles the standard FTMS flags plus KingSmith's
 * proprietary step-count extension at flag bit 13.
 *
 * References:
 *   - Bluetooth SIG FTMS specification (GASS v1.0)
 *   - mcdax/walkingpad-controller (Python, reverse-engineered)
 *   - mcdax/hass-walkingpad (Home Assistant integration)
 */

#include "walkingpad_ftms.h"
#include <string.h>

/* ── Helper: read little-endian uint16 from buffer ──────────────────── */

static inline uint16_t read_u16_le(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_u24_le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

/* ── FTMS Treadmill Data flag bits ──────────────────────────────────── */

#define FLAG_MORE_DATA (1 << 0)
#define FLAG_AVG_SPEED (1 << 1)
#define FLAG_TOTAL_DISTANCE (1 << 2)
#define FLAG_INCLINATION (1 << 3)
#define FLAG_ELEVATION_GAIN (1 << 4)
#define FLAG_INST_PACE (1 << 5)
#define FLAG_AVG_PACE (1 << 6)
#define FLAG_EXPENDED_ENERGY (1 << 7)
#define FLAG_HEART_RATE (1 << 8)
#define FLAG_METABOLIC_EQ (1 << 9)
#define FLAG_ELAPSED_TIME (1 << 10)
#define FLAG_REMAINING_TIME (1 << 11)
#define FLAG_FORCE_ON_BELT (1 << 12)
#define FLAG_KS_STEPS (1 << 13) /* KingSmith proprietary extension */

/* ── Encode commands ────────────────────────────────────────────────── */

size_t ftms_encode_request_control(uint8_t *buf, size_t buf_size) {
  if (buf_size < 1)
    return 0;
  buf[0] = FTMS_CP_REQUEST_CONTROL;
  return 1;
}

size_t ftms_encode_set_speed(uint8_t *buf, size_t buf_size, float speed_kmh) {
  if (buf_size < 3)
    return 0;
  if (speed_kmh < 0.0f)
    speed_kmh = 0.0f;
  if (speed_kmh > 20.0f)
    speed_kmh = 20.0f; /* generous upper limit */
  uint16_t speed_001 = (uint16_t)(speed_kmh * 100.0f + 0.5f);
  buf[0] = FTMS_CP_SET_TARGET_SPEED;
  buf[1] = (uint8_t)(speed_001 & 0xFF);
  buf[2] = (uint8_t)(speed_001 >> 8);
  return 3;
}

size_t ftms_encode_start(uint8_t *buf, size_t buf_size) {
  if (buf_size < 1)
    return 0;
  buf[0] = FTMS_CP_START_RESUME;
  return 1;
}

size_t ftms_encode_stop(uint8_t *buf, size_t buf_size) {
  if (buf_size < 2)
    return 0;
  buf[0] = FTMS_CP_STOP_PAUSE;
  buf[1] = 0x01; /* 0x01 = stop */
  return 2;
}

size_t ftms_encode_reset(uint8_t *buf, size_t buf_size) {
  if (buf_size < 1)
    return 0;
  buf[0] = FTMS_CP_RESET;
  return 1;
}

/* ── Decode Treadmill Data (0x2ACD) ─────────────────────────────────── */

bool ftms_decode_treadmill_data(const uint8_t *buf, size_t len,
                                WalkingPadStatus *status) {
  if (len < 4)
    return false; /* minimum: 2 bytes flags + 2 bytes speed */

  memset(status, 0, sizeof(WalkingPadStatus));
  status->mode = WalkingPadModeManual; /* FTMS is always "manual" */

  size_t offset = 0;
  uint16_t flags = read_u16_le(&buf[offset]);
  offset += 2;

  /* Instantaneous Speed is always present (uint16 LE, 0.01 km/h) */
  if (offset + 2 > len)
    return false;
  uint16_t speed_raw = read_u16_le(&buf[offset]);
  status->speed_kmh = (float)speed_raw / 100.0f;
  offset += 2;

  /* Average Speed (bit 1) — uint16 */
  if (flags & FLAG_AVG_SPEED) {
    offset += 2;
  }

  /* Total Distance (bit 2) — uint24 LE, metres */
  if (flags & FLAG_TOTAL_DISTANCE) {
    if (offset + 3 > len)
      return true; /* partial is ok */
    uint32_t dist_m = read_u24_le(&buf[offset]);
    status->distance_km = (float)dist_m / 1000.0f;
    offset += 3;
  }

  /* Inclination + Ramp Angle (bit 3) — 2 × int16 */
  if (flags & FLAG_INCLINATION) {
    offset += 4;
  }

  /* Elevation Gain (bit 4) — 2 × uint16 */
  if (flags & FLAG_ELEVATION_GAIN) {
    offset += 4;
  }

  /* Instantaneous Pace (bit 5) — uint8 */
  if (flags & FLAG_INST_PACE) {
    offset += 1;
  }

  /* Average Pace (bit 6) — uint8 */
  if (flags & FLAG_AVG_PACE) {
    offset += 1;
  }

  /* Expended Energy (bit 7) — uint16 total + uint16 per_hour + uint8 per_min */
  if (flags & FLAG_EXPENDED_ENERGY) {
    if (offset + 2 <= len) {
      status->calories = read_u16_le(&buf[offset]);
    }
    offset += 5; /* total(2) + per_hour(2) + per_min(1) */
  }

  /* Heart Rate (bit 8) — uint8 */
  if (flags & FLAG_HEART_RATE) {
    offset += 1;
  }

  /* Metabolic Equivalent (bit 9) — uint8 */
  if (flags & FLAG_METABOLIC_EQ) {
    offset += 1;
  }

  /* Elapsed Time (bit 10) — uint16, seconds */
  if (flags & FLAG_ELAPSED_TIME) {
    if (offset + 2 <= len) {
      status->time_seconds = read_u16_le(&buf[offset]);
    }
    offset += 2;
  }

  /* Remaining Time (bit 11) — uint16 */
  if (flags & FLAG_REMAINING_TIME) {
    offset += 2;
  }

  /* Force on Belt + Power (bit 12) — 2 × int16 */
  if (flags & FLAG_FORCE_ON_BELT) {
    offset += 4;
  }

  /* KingSmith proprietary step count (bit 13) — uint16 + 1 pad byte */
  if (flags & FLAG_KS_STEPS) {
    if (offset + 2 <= len) {
      status->steps = read_u16_le(&buf[offset]);
    }
    offset += 3; /* steps(2) + pad(1) */
  }

  (void)offset; /* suppress unused warning */
  return true;
}

/* ── Decode Supported Speed Range (0x2AD4) ──────────────────────────── */

bool ftms_decode_speed_range(const uint8_t *buf, size_t len,
                             WalkingPadFtmsSpeedRange *range) {
  if (len < 6)
    return false;
  range->min_speed_001 = read_u16_le(&buf[0]);
  range->max_speed_001 = read_u16_le(&buf[2]);
  range->increment_001 = read_u16_le(&buf[4]);
  return true;
}
