#include "../protocol/walkingpad_protocol.h"
#include "minunit.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int tests_run = 0;
int tests_failed = 0;

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

static uint8_t compute_crc(const uint8_t *frame) {
  /* Matches walkingpad_fix_crc logic for 6-byte frames */
  return frame[1] + frame[2] + frame[3];
}

/* ========================================================================= */
/* Command encoding tests                                                    */
/* ========================================================================= */

static const char *test_encode_ask_stats() {
  uint8_t buf[16];
  size_t n;

  memset(buf, 0xAA, sizeof(buf));
  n = walkingpad_encode_cmd_ask_stats(buf, sizeof(buf));
  mu_assert_eq(6, n);
  mu_assert_eq(0xF7, buf[0]);
  mu_assert_eq(0xA2, buf[1]);
  mu_assert_eq(0x00, buf[2]);
  mu_assert_eq(0x00, buf[3]);
  mu_assert_eq(0xFD, buf[5]);
  mu_assert_eq(compute_crc(buf), buf[4]);

  /* Buffer too small */
  n = walkingpad_encode_cmd_ask_stats(buf, 5);
  mu_assert_eq(0, n);

  return NULL;
}

static const char *test_encode_set_speed() {
  uint8_t buf[16];
  size_t n;

  /* Normal value: 3.5 km/h -> 35 deci-kmh */
  memset(buf, 0xAA, sizeof(buf));
  n = walkingpad_encode_cmd_set_speed(buf, sizeof(buf), 3.5f);
  mu_assert_eq(6, n);
  mu_assert_eq(0xF7, buf[0]);
  mu_assert_eq(0xA2, buf[1]);
  mu_assert_eq(0x01, buf[2]);
  mu_assert_eq(35, buf[3]);
  mu_assert_eq(0xFD, buf[5]);
  mu_assert_eq(compute_crc(buf), buf[4]);

  /* Zero speed */
  memset(buf, 0xAA, sizeof(buf));
  n = walkingpad_encode_cmd_set_speed(buf, sizeof(buf), 0.0f);
  mu_assert_eq(6, n);
  mu_assert_eq(0x00, buf[3]);
  mu_assert_eq(compute_crc(buf), buf[4]);

  /* Max speed (clamped) 6.0 km/h -> 60 */
  memset(buf, 0xAA, sizeof(buf));
  n = walkingpad_encode_cmd_set_speed(buf, sizeof(buf), 6.0f);
  mu_assert_eq(6, n);
  mu_assert_eq(60, buf[3]);

  /* Above max -> clamped */
  memset(buf, 0xAA, sizeof(buf));
  n = walkingpad_encode_cmd_set_speed(buf, sizeof(buf), 9.9f);
  mu_assert_eq(6, n);
  mu_assert_eq(60, buf[3]);

  /* Below min -> clamped */
  memset(buf, 0xAA, sizeof(buf));
  n = walkingpad_encode_cmd_set_speed(buf, sizeof(buf), -1.0f);
  mu_assert_eq(6, n);
  mu_assert_eq(0, buf[3]);

  /* Fractional speed: 2.3 km/h -> 23 */
  memset(buf, 0xAA, sizeof(buf));
  n = walkingpad_encode_cmd_set_speed(buf, sizeof(buf), 2.3f);
  mu_assert_eq(23, buf[3]);

  /* Buffer too small */
  n = walkingpad_encode_cmd_set_speed(buf, 5, 1.0f);
  mu_assert_eq(0, n);

  return NULL;
}

static const char *test_encode_set_mode() {
  uint8_t buf[16];
  size_t n;

  for (int mode = 0; mode < 8; mode++) {
    memset(buf, 0xAA, sizeof(buf));
    n = walkingpad_encode_cmd_set_mode(buf, sizeof(buf), (WalkingPadMode)mode);
    mu_assert_eq(6, n);
    mu_assert_eq(0xF7, buf[0]);
    mu_assert_eq(0xA2, buf[1]);
    mu_assert_eq(0x02, buf[2]);
    mu_assert_eq(mode, buf[3]);
    mu_assert_eq(0xFD, buf[5]);
    mu_assert_eq(compute_crc(buf), buf[4]);
  }

  /* Buffer too small */
  n = walkingpad_encode_cmd_set_mode(buf, 5, WalkingPadModeAuto);
  mu_assert_eq(0, n);

  return NULL;
}

static const char *test_encode_start_belt() {
  uint8_t buf[16];
  size_t n;

  memset(buf, 0xAA, sizeof(buf));
  n = walkingpad_encode_cmd_start_belt(buf, sizeof(buf));
  mu_assert_eq(6, n);
  mu_assert_eq(0xF7, buf[0]);
  mu_assert_eq(0xA2, buf[1]);
  mu_assert_eq(0x04, buf[2]);
  mu_assert_eq(0x01, buf[3]);
  mu_assert_eq(0xFD, buf[5]);
  mu_assert_eq(compute_crc(buf), buf[4]);

  /* Buffer too small */
  n = walkingpad_encode_cmd_start_belt(buf, 5);
  mu_assert_eq(0, n);

  return NULL;
}

static const char *test_encode_stop_belt() {
  uint8_t buf[16];
  uint8_t ref[16];

  /* Stop belt must produce the same frame as set_speed(0) */
  walkingpad_encode_cmd_stop_belt(buf, sizeof(buf));
  walkingpad_encode_cmd_set_speed(ref, sizeof(ref), 0.0f);
  mu_assert_mem_eq(ref, buf, 6);

  return NULL;
}

/* ========================================================================= */
/* Status decoding tests                                                     */
/* ========================================================================= */

static const char *test_decode_status_basic() {
  uint8_t frame[14] = {
      0xF8, 0xA2,       /* header */
      0x00,             /* payload[0] ignored */
      0x23,             /* payload[1] speed = 3.5 km/h (35 / 10) */
      0x01,             /* payload[2] mode = manual */
      0x00, 0x01, 0x2C, /* payload[3..5] time = 300 s (0x12C) BE */
      0x00, 0x00, 0x64, /* payload[6..8] dist = 100 hundredths = 1.00 km */
      0x00, 0x00, 0x0A  /* payload[9..11] steps = 10 */
  };

  WalkingPadStatus st;
  memset(&st, 0xAA, sizeof(st));

  bool ok = walkingpad_decode_status(frame, sizeof(frame), &st);
  mu_assert_eq(1, ok);
  mu_assert_feq(3.5f, st.speed_kmh, 0.001f);
  mu_assert_eq(WalkingPadModeManual, st.mode);
  mu_assert_eq_uint32(300, st.time_seconds);
  mu_assert_feq(1.0f, st.distance_km, 0.001f);
  mu_assert_eq_uint32(10, st.steps);

  return NULL;
}

static const char *test_decode_status_max_values() {
  /* Test maximum 24-bit values */
  uint8_t frame[14] = {
      0xF8, 0xA2, 0x00, 0x3C, /* speed = 6.0 km/h (60 / 10) */
      0x04,                   /* calibration mode */
      0xFF, 0xFF, 0xFF,       /* time = 16,777,215 s */
      0xFF, 0xFF, 0xFF,       /* dist = 16,777,215 hundredths = 167,772.15 km */
      0xFF, 0xFF, 0xFF        /* steps = 16,777,215 */
  };

  WalkingPadStatus st;
  bool ok = walkingpad_decode_status(frame, sizeof(frame), &st);
  mu_assert_eq(1, ok);
  mu_assert_feq(6.0f, st.speed_kmh, 0.001f);
  mu_assert_eq(WalkingPadModeCalibration, st.mode);
  mu_assert_eq_uint32(0xFFFFFF, st.time_seconds);
  mu_assert_feq(167772.15f, st.distance_km, 0.01f);
  mu_assert_eq_uint32(0xFFFFFF, st.steps);

  return NULL;
}

static const char *test_decode_status_zero_values() {
  uint8_t frame[14] = {0xF8, 0xA2, 0xFF, /* payload[0] can be anything */
                       0x00,             /* speed 0 */
                       0x02,             /* standby */
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  WalkingPadStatus st;
  bool ok = walkingpad_decode_status(frame, sizeof(frame), &st);
  mu_assert_eq(1, ok);
  mu_assert_feq(0.0f, st.speed_kmh, 0.001f);
  mu_assert_eq(WalkingPadModeStandby, st.mode);
  mu_assert_eq_uint32(0, st.time_seconds);
  mu_assert_feq(0.0f, st.distance_km, 0.001f);
  mu_assert_eq_uint32(0, st.steps);

  return NULL;
}

static const char *test_decode_status_longer_frame() {
  /* Some models send >14 bytes; we should accept 14+ */
  uint8_t frame[20] = {
      0xF8, 0xA2, 0x00, 0x0A,            /* speed = 1.0 km/h */
      0x00,                              /* auto mode */
      0x00, 0x00, 0x3C,                  /* time = 60 s */
      0x00, 0x00, 0x0A,                  /* dist = 0.10 km */
      0x00, 0x00, 0x05,                  /* steps = 5 */
      0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE /* trailing garbage */
  };

  WalkingPadStatus st;
  bool ok = walkingpad_decode_status(frame, sizeof(frame), &st);
  mu_assert_eq(1, ok);
  mu_assert_feq(1.0f, st.speed_kmh, 0.001f);
  mu_assert_eq(WalkingPadModeAuto, st.mode);
  mu_assert_eq_uint32(60, st.time_seconds);
  mu_assert_feq(0.1f, st.distance_km, 0.001f);
  mu_assert_eq_uint32(5, st.steps);

  return NULL;
}

static const char *test_decode_status_invalid() {
  WalkingPadStatus st;
  uint8_t frame[14];
  memset(frame, 0x00, sizeof(frame));

  /* Wrong header byte 0 */
  frame[0] = 0xF9;
  frame[1] = 0xA2;
  bool ok = walkingpad_decode_status(frame, sizeof(frame), &st);
  mu_assert_eq(0, ok);

  /* Wrong header byte 1 */
  frame[0] = 0xF8;
  frame[1] = 0xA3;
  ok = walkingpad_decode_status(frame, sizeof(frame), &st);
  mu_assert_eq(0, ok);

  /* Too short: 13 bytes */
  frame[0] = 0xF8;
  frame[1] = 0xA2;
  ok = walkingpad_decode_status(frame, 13, &st);
  mu_assert_eq(0, ok);

  /* Too short payload: 14 bytes total but only 11 payload bytes after header */
  /* Actually 14 total means 12 payload bytes, which is enough. Need 13 total
   * for 11 payload. */
  frame[0] = 0xF8;
  frame[1] = 0xA2;
  ok = walkingpad_decode_status(frame, 13, &st);
  mu_assert_eq(0, ok);

  return NULL;
}

/* ========================================================================= */
/* CRC-specific tests                                                        */
/* ========================================================================= */

static const char *test_crc_overflow() {
  /* Verify CRC computation indirectly through encoders. */
  uint8_t out[6];

  /* set_speed(6.0): 0xA2 + 0x01 + 0x3C = 0xDF */
  walkingpad_encode_cmd_set_speed(out, sizeof(out), 6.0f);
  mu_assert_eq(0xDF, out[4]);

  /* start belt: 0xA2 + 0x04 + 0x01 = 0xA7 */
  walkingpad_encode_cmd_start_belt(out, sizeof(out));
  mu_assert_eq(0xA7, out[4]);

  /* ask stats: 0xA2 + 0x00 + 0x00 = 0xA2 */
  walkingpad_encode_cmd_ask_stats(out, sizeof(out));
  mu_assert_eq(0xA2, out[4]);

  /* set_mode(manual): 0xA2 + 0x02 + 0x01 = 0xA5 */
  walkingpad_encode_cmd_set_mode(out, sizeof(out), WalkingPadModeManual);
  mu_assert_eq(0xA5, out[4]);

  return NULL;
}

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

int main(void) {
  printf("Running WalkingPad Protocol Tests\n");
  printf("=================================\n\n");

  mu_run_test(test_encode_ask_stats);
  mu_run_test(test_encode_set_speed);
  mu_run_test(test_encode_set_mode);
  mu_run_test(test_encode_start_belt);
  mu_run_test(test_encode_stop_belt);
  mu_run_test(test_decode_status_basic);
  mu_run_test(test_decode_status_max_values);
  mu_run_test(test_decode_status_zero_values);
  mu_run_test(test_decode_status_longer_frame);
  mu_run_test(test_decode_status_invalid);
  mu_run_test(test_crc_overflow);

  printf("\n=================================\n");
  printf("Results: %d run, %d passed, %d failed\n", tests_run,
         tests_run - tests_failed, tests_failed);

  return tests_failed > 0 ? 1 : 0;
}
