#include "../protocol/walkingpad_ftms.h"
#include "minunit.h"
#include <stdio.h>
#include <string.h>

int tests_run = 0;
int tests_failed = 0;

/* ── Encode tests ───────────────────────────────────────────────────── */

static char *test_ftms_encode_request_control(void) {
  uint8_t buf[4];
  size_t n = ftms_encode_request_control(buf, sizeof(buf));
  mu_assert_eq(1, (int)n);
  mu_assert_eq(0x00, buf[0]);
  return 0;
}

static char *test_ftms_encode_start(void) {
  uint8_t buf[4];
  size_t n = ftms_encode_start(buf, sizeof(buf));
  mu_assert_eq(1, (int)n);
  mu_assert_eq(0x07, buf[0]);
  return 0;
}

static char *test_ftms_encode_stop(void) {
  uint8_t buf[4];
  size_t n = ftms_encode_stop(buf, sizeof(buf));
  mu_assert_eq(2, (int)n);
  mu_assert_eq(0x08, buf[0]);
  mu_assert_eq(0x01, buf[1]);
  return 0;
}

static char *test_ftms_encode_set_speed(void) {
  uint8_t buf[4];
  /* 4.0 km/h = 400 = 0x0190 */
  size_t n = ftms_encode_set_speed(buf, sizeof(buf), 4.0f);
  mu_assert_eq(3, (int)n);
  mu_assert_eq(0x02, buf[0]);
  mu_assert_eq(0x90, buf[1]);
  mu_assert_eq(0x01, buf[2]);
  return 0;
}

static char *test_ftms_encode_set_speed_max(void) {
  uint8_t buf[4];
  /* 6.0 km/h = 600 = 0x0258 */
  size_t n = ftms_encode_set_speed(buf, sizeof(buf), 6.0f);
  mu_assert_eq(3, (int)n);
  uint16_t speed = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
  mu_assert_eq(600, (int)speed);
  return 0;
}

static char *test_ftms_encode_reset(void) {
  uint8_t buf[4];
  size_t n = ftms_encode_reset(buf, sizeof(buf));
  mu_assert_eq(1, (int)n);
  mu_assert_eq(0x01, buf[0]);
  return 0;
}

static char *test_ftms_encode_buf_too_small(void) {
  uint8_t buf[1];
  size_t n = ftms_encode_set_speed(buf, sizeof(buf), 3.0f);
  mu_assert_eq(0, (int)n);
  return 0;
}

/* ── Decode tests ───────────────────────────────────────────────────── */

static char *test_ftms_decode_speed_only(void) {
  /* Flags=0x0000, speed=350 (3.5 km/h) */
  uint8_t data[] = {0x00, 0x00, 0x5E, 0x01};
  WalkingPadStatus st;
  mu_assert_eq(1, ftms_decode_treadmill_data(data, sizeof(data), &st));
  mu_assert_feq(3.5f, st.speed_kmh, 0.01f);
  mu_assert_eq(WalkingPadModeManual, st.mode);
  mu_assert_eq(0, (int)st.steps);
  mu_assert_eq(0, (int)st.time_seconds);
  return 0;
}

static char *test_ftms_decode_with_distance(void) {
  /* Flags=0x0004, speed=200 (2.0), dist=1500m */
  uint8_t data[] = {
      0x04, 0x00, 0xC8, 0x00, 0xDC, 0x05, 0x00,
  };
  WalkingPadStatus st;
  mu_assert_eq(1, ftms_decode_treadmill_data(data, sizeof(data), &st));
  mu_assert_feq(2.0f, st.speed_kmh, 0.01f);
  mu_assert_feq(1.5f, st.distance_km, 0.01f);
  return 0;
}

static char *test_ftms_decode_with_energy_and_time(void) {
  /* Flags=0x0480 (energy bit 7 + elapsed time bit 10) */
  uint8_t data[] = {
      0x80, 0x04, 0x64, 0x00, 0x2A, 0x00, 0x78, 0x00, 0x02, /* energy */
      0x2C, 0x01,                                           /* time 300s */
  };
  WalkingPadStatus st;
  mu_assert_eq(1, ftms_decode_treadmill_data(data, sizeof(data), &st));
  mu_assert_feq(1.0f, st.speed_kmh, 0.01f);
  mu_assert_eq(42, (int)st.calories);
  mu_assert_eq_uint32(300, st.time_seconds);
  return 0;
}

static char *test_ftms_decode_with_ks_steps(void) {
  /* Flags=0x2000 (KingSmith step extension bit 13) */
  uint8_t data[] = {
      0x00, 0x20, 0xF4, 0x01, 0xE8, 0x03, 0x00, /* steps 1000 + pad */
  };
  WalkingPadStatus st;
  mu_assert_eq(1, ftms_decode_treadmill_data(data, sizeof(data), &st));
  mu_assert_feq(5.0f, st.speed_kmh, 0.01f);
  mu_assert_eq_uint32(1000, st.steps);
  return 0;
}

static char *test_ftms_decode_full(void) {
  /* Flags=0x2484 (distance + energy + time + KS steps) */
  uint8_t data[] = {
      0x84, 0x24, 0x90, 0x01,       /* speed 400 = 4.0 km/h */
      0xE8, 0x03, 0x00,             /* distance 1000m */
      0x55, 0x00, 0xC8, 0x00, 0x03, /* energy 85/200/3 */
      0x58, 0x02,                   /* time 600s */
      0xC4, 0x09, 0x00,             /* steps 2500 + pad */
  };
  WalkingPadStatus st;
  mu_assert_eq(1, ftms_decode_treadmill_data(data, sizeof(data), &st));
  mu_assert_feq(4.0f, st.speed_kmh, 0.01f);
  mu_assert_feq(1.0f, st.distance_km, 0.01f);
  mu_assert_eq(85, (int)st.calories);
  mu_assert_eq_uint32(600, st.time_seconds);
  mu_assert_eq_uint32(2500, st.steps);
  mu_assert_eq(WalkingPadModeManual, st.mode);
  return 0;
}

static char *test_ftms_decode_too_short(void) {
  uint8_t data[] = {0x00, 0x00, 0x64};
  WalkingPadStatus st;
  mu_assert_eq(0, ftms_decode_treadmill_data(data, sizeof(data), &st));
  return 0;
}

static char *test_ftms_decode_speed_range(void) {
  /* min=100, max=600, inc=10 */
  uint8_t data[] = {0x64, 0x00, 0x58, 0x02, 0x0A, 0x00};
  WalkingPadFtmsSpeedRange range;
  mu_assert_eq(1, ftms_decode_speed_range(data, sizeof(data), &range));
  mu_assert_eq(100, (int)range.min_speed_001);
  mu_assert_eq(600, (int)range.max_speed_001);
  mu_assert_eq(10, (int)range.increment_001);
  return 0;
}

static char *test_ftms_decode_speed_range_short(void) {
  uint8_t data[] = {0x64, 0x00, 0x58};
  WalkingPadFtmsSpeedRange range;
  mu_assert_eq(0, ftms_decode_speed_range(data, sizeof(data), &range));
  return 0;
}

/* ── Runner ─────────────────────────────────────────────────────────── */

static char *all_tests(void) {
  mu_run_test(test_ftms_encode_request_control);
  mu_run_test(test_ftms_encode_start);
  mu_run_test(test_ftms_encode_stop);
  mu_run_test(test_ftms_encode_set_speed);
  mu_run_test(test_ftms_encode_set_speed_max);
  mu_run_test(test_ftms_encode_reset);
  mu_run_test(test_ftms_encode_buf_too_small);
  mu_run_test(test_ftms_decode_speed_only);
  mu_run_test(test_ftms_decode_with_distance);
  mu_run_test(test_ftms_decode_with_energy_and_time);
  mu_run_test(test_ftms_decode_with_ks_steps);
  mu_run_test(test_ftms_decode_full);
  mu_run_test(test_ftms_decode_too_short);
  mu_run_test(test_ftms_decode_speed_range);
  mu_run_test(test_ftms_decode_speed_range_short);
  return 0;
}

int main(void) {
  printf("Running FTMS Protocol Tests\n");
  printf("===========================\n\n");
  char *result = all_tests();
  printf("\n===========================\n");
  if (result) {
    printf("FAILED: %s\n", result);
  }
  printf("Results: %d run, %d passed, %d failed\n", tests_run,
         tests_run - tests_failed, tests_failed);
  return result != 0;
}
