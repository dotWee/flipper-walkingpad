/**
 * WalkingPad A1 Pro protocol implementation.
 *
 * Commands are 6-byte frames:
 *   [0] = 0xF7 (start)
 *   [1] = 0xA2 (type)
 *   [2] = action
 *   [3] = value
 *   [4] = CRC  (sum of bytes [1..len-3], overflow intended)
 *   [5] = 0xFD (footer)
 *
 * Status responses are variable-length but typically 12+ bytes with header 0xF8
 * 0xA2.
 */

#include "walkingpad_protocol.h"

static void walkingpad_fix_crc(uint8_t *cmd, size_t len) {
  if (len < 2)
    return;
  uint8_t sum = 0;
  for (size_t i = 1; i < len - 2; i++) {
    sum += cmd[i];
  }
  cmd[len - 2] = sum;
}

size_t walkingpad_encode_cmd_ask_stats(uint8_t *buf, size_t buf_size) {
  if (buf_size < 6)
    return 0;
  buf[0] = 0xF7;
  buf[1] = 0xA2;
  buf[2] = 0x00;
  buf[3] = 0x00;
  buf[5] = 0xFD;
  walkingpad_fix_crc(buf, 6);
  return 6;
}

size_t walkingpad_encode_cmd_set_speed(uint8_t *buf, size_t buf_size,
                                       float speed_kmh) {
  if (buf_size < 6)
    return 0;
  if (speed_kmh < 0.0f)
    speed_kmh = 0.0f;
  if (speed_kmh > 6.0f)
    speed_kmh = 6.0f;
  uint8_t val = (uint8_t)(speed_kmh * 10.0f);
  buf[0] = 0xF7;
  buf[1] = 0xA2;
  buf[2] = 0x01;
  buf[3] = val;
  buf[5] = 0xFD;
  walkingpad_fix_crc(buf, 6);
  return 6;
}

size_t walkingpad_encode_cmd_set_mode(uint8_t *buf, size_t buf_size,
                                      WalkingPadMode mode) {
  if (buf_size < 6)
    return 0;
  buf[0] = 0xF7;
  buf[1] = 0xA2;
  buf[2] = 0x02;
  buf[3] = (uint8_t)mode;
  buf[5] = 0xFD;
  walkingpad_fix_crc(buf, 6);
  return 6;
}

size_t walkingpad_encode_cmd_start_belt(uint8_t *buf, size_t buf_size) {
  if (buf_size < 6)
    return 0;
  buf[0] = 0xF7;
  buf[1] = 0xA2;
  buf[2] = 0x04;
  buf[3] = 0x01;
  buf[5] = 0xFD;
  walkingpad_fix_crc(buf, 6);
  return 6;
}

size_t walkingpad_encode_cmd_stop_belt(uint8_t *buf, size_t buf_size) {
  return walkingpad_encode_cmd_set_speed(buf, buf_size, 0.0f);
}

bool walkingpad_decode_status(const uint8_t *buf, size_t len,
                              WalkingPadStatus *status) {
  /* We expect at least enough bytes for the shortened status frame.
   * Reference implementations treat the payload after F8 A2 as:
   *   [0] ??? (sometimes 0)
   *   [1] speed (0.1 km/h)
   *   [2] mode
   *   [3..5] time (24-bit BE, seconds)
   *   [6..8] distance (24-bit BE, hundredths of km -> 10 m units)
   *   [9..11] steps (24-bit BE)
   *
   * Some WalkingPad variants send longer frames (14 bytes).  We'll
   * be lenient and accept anything >= 12 bytes after the header.
   */
  if (len < 14)
    return false;
  if (buf[0] != 0xF8 || buf[1] != 0xA2)
    return false;

  /* Determine payload offset.  Some models send an extra status byte at [2].
   * The Go reference treats buf[2..] as the payload, with speeds at [3].
   * tim-oster code: readStatusBuffer(buf[2:])
   *   speed = buf[1] / 10      -> payload[1]
   *   mode  = buf[2]           -> payload[2]
   *   time  = buf[3]<<16 ...   -> payload[3..5]
   *   dist  = buf[6]<<16 ...   -> payload[6..8]
   *   steps = buf[9]<<16 ...   -> payload[9..11]
   *
   * That means payload starts at buf[2] in the raw buffer,
   * so buf[2] is payload[0] (ignored), buf[3] is payload[1] (speed), etc.
   * We'll match that convention.
   */
  const uint8_t *p = &buf[2];
  if (len - 2 < 12)
    return false;

  uint32_t time_s =
      ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | (uint32_t)p[5];
  uint32_t dist =
      ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 8) | (uint32_t)p[8];
  uint32_t steps =
      ((uint32_t)p[9] << 16) | ((uint32_t)p[10] << 8) | (uint32_t)p[11];

  status->speed_kmh = (float)p[1] / 10.0f;
  status->mode = (WalkingPadMode)p[2];
  status->time_seconds = time_s;
  status->distance_km = (float)dist / 100.0f;
  status->steps = steps;
  status->calories = 0; /* legacy protocol does not report calories */
  return true;
}
