#pragma once

#include "walkingpad_ftms.h"
#include "walkingpad_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Protocol-agnostic command API.
 * Dispatches to the correct encoder based on detected protocol type.
 */

size_t walkingpad_proto_encode_set_speed(WalkingPadProtoType proto,
                                         uint8_t *buf, size_t buf_size,
                                         float speed_kmh);

size_t walkingpad_proto_encode_start(WalkingPadProtoType proto, uint8_t *buf,
                                     size_t buf_size);

size_t walkingpad_proto_encode_stop(WalkingPadProtoType proto, uint8_t *buf,
                                    size_t buf_size);

size_t walkingpad_proto_encode_set_mode(WalkingPadProtoType proto, uint8_t *buf,
                                        size_t buf_size, WalkingPadMode mode);

/** Legacy only — FTMS pushes data via notifications, returns 0. */
size_t walkingpad_proto_encode_ask_stats(WalkingPadProtoType proto,
                                         uint8_t *buf, size_t buf_size);

/** Returns true if the protocol supports auto/manual mode switching. */
bool walkingpad_proto_supports_mode(WalkingPadProtoType proto);

#ifdef __cplusplus
}
#endif
