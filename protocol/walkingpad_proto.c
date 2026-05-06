/**
 * Protocol dispatcher — routes command encoding to the correct
 * codec (legacy WiLink or FTMS) based on detected protocol type.
 */

#include "walkingpad_proto.h"

size_t walkingpad_proto_encode_set_speed(WalkingPadProtoType proto,
                                         uint8_t *buf, size_t buf_size,
                                         float speed_kmh) {
  switch (proto) {
  case WalkingPadProtoLegacy:
    return walkingpad_encode_cmd_set_speed(buf, buf_size, speed_kmh);
  case WalkingPadProtoFTMS:
    return ftms_encode_set_speed(buf, buf_size, speed_kmh);
  default:
    return 0;
  }
}

size_t walkingpad_proto_encode_start(WalkingPadProtoType proto, uint8_t *buf,
                                     size_t buf_size) {
  switch (proto) {
  case WalkingPadProtoLegacy:
    return walkingpad_encode_cmd_start_belt(buf, buf_size);
  case WalkingPadProtoFTMS:
    return ftms_encode_start(buf, buf_size);
  default:
    return 0;
  }
}

size_t walkingpad_proto_encode_stop(WalkingPadProtoType proto, uint8_t *buf,
                                    size_t buf_size) {
  switch (proto) {
  case WalkingPadProtoLegacy:
    return walkingpad_encode_cmd_stop_belt(buf, buf_size);
  case WalkingPadProtoFTMS:
    return ftms_encode_stop(buf, buf_size);
  default:
    return 0;
  }
}

size_t walkingpad_proto_encode_set_mode(WalkingPadProtoType proto, uint8_t *buf,
                                        size_t buf_size, WalkingPadMode mode) {
  switch (proto) {
  case WalkingPadProtoLegacy:
    return walkingpad_encode_cmd_set_mode(buf, buf_size, mode);
  case WalkingPadProtoFTMS:
    return 0; /* FTMS has no mode concept */
  default:
    return 0;
  }
}

size_t walkingpad_proto_encode_ask_stats(WalkingPadProtoType proto,
                                         uint8_t *buf, size_t buf_size) {
  switch (proto) {
  case WalkingPadProtoLegacy:
    return walkingpad_encode_cmd_ask_stats(buf, buf_size);
  case WalkingPadProtoFTMS:
    return 0; /* FTMS pushes data via notifications */
  default:
    return 0;
  }
}

bool walkingpad_proto_supports_mode(WalkingPadProtoType proto) {
  return (proto == WalkingPadProtoLegacy);
}
