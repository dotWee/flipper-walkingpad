#ifndef WALKINGPAD_APP_H
#define WALKINGPAD_APP_H

#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/submenu.h>
#include <gui/view_dispatcher.h>
#include <notification/notification_messages.h>

#include "ble/walkingpad_ble.h"
#include "protocol/walkingpad_proto.h"
#include "protocol/walkingpad_protocol.h"

#define WALKINGPAD_APP_TAG "WalkingPad"

/* Default speed limits for legacy protocol (tenths of km/h) */
#define WALKINGPAD_DEFAULT_MAX_SPEED_TENTHS (60)
#define WALKINGPAD_DEFAULT_MIN_SPEED_TENTHS (5)
#define WALKINGPAD_DEFAULT_SPEED_STEP (5)

/* Forward declare view struct from views/ */
typedef struct WalkingPadMainView WalkingPadMainView;

typedef struct {
  Gui *gui;
  ViewDispatcher *view_dispatcher;
  WalkingPadMainView *main_view;

  /* BLE transport */
  WalkingPadBle *ble;
  FuriStreamBuffer *rx_stream;
  FuriThread *worker_thread;

  /* State - protected by mutex */
  FuriMutex *mutex;
  WalkingPadBleState ble_state;
  WalkingPadProtoType active_proto;
  bool running;
  WalkingPadStatus status;
  uint8_t target_speed_tenths;

  /* Dynamic speed limits (populated from device or defaults) */
  uint16_t min_speed_tenths;
  uint16_t max_speed_tenths;
  uint16_t speed_step_tenths;
} WalkingPadApp;

/* App entry point */
int32_t walkingpad_app(void *p);

#endif
