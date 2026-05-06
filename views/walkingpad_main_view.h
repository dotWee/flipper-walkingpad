#pragma once

#include "../ble/walkingpad_ble.h"
#include "../protocol/walkingpad_protocol.h"
#include <gui/view.h>

typedef struct WalkingPadMainView WalkingPadMainView;

WalkingPadMainView *walkingpad_main_view_alloc();
void walkingpad_main_view_free(WalkingPadMainView *view);
View *walkingpad_main_view_get_view(WalkingPadMainView *view);

void walkingpad_main_view_update_state(WalkingPadMainView *view,
                                       WalkingPadBleState ble_state,
                                       WalkingPadProtoType proto,
                                       const WalkingPadStatus *status,
                                       uint8_t target_speed_tenths);

typedef enum {
  WalkingPadMainViewInputResultConnect,
  WalkingPadMainViewInputResultDisconnect,
  WalkingPadMainViewInputResultStart,
  WalkingPadMainViewInputResultStop,
  WalkingPadMainViewInputResultSpeedUp,
  WalkingPadMainViewInputResultSpeedDown,
  WalkingPadMainViewInputResultToggleMode,
} WalkingPadMainViewInputResult;

typedef void (*WalkingPadMainViewCallback)(WalkingPadMainViewInputResult result,
                                           void *context);

void walkingpad_main_view_set_callback(WalkingPadMainView *view,
                                       WalkingPadMainViewCallback cb,
                                       void *context);
