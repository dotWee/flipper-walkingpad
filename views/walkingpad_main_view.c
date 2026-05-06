#include "walkingpad_main_view.h"
#include "../protocol/walkingpad_proto.h"
#include <gui/canvas.h>
#include <input/input.h>

typedef struct {
  WalkingPadBleState ble_state;
  WalkingPadProtoType proto;
  WalkingPadStatus status;
  uint8_t target_speed_tenths;
  int selected_item;

  WalkingPadMainViewCallback cb;
  void *cb_ctx;
} WalkingPadMainViewModel;

struct WalkingPadMainView {
  View *view;
};

typedef enum {
  ItemConnect = 0,
  ItemDisconnect,
  ItemStart,
  ItemStop,
  ItemSpeedDown,
  ItemSpeedUp,
  ItemToggleMode,
  ItemCount,
} MenuItem;

static const char *ble_state_str(WalkingPadBleState state,
                                 WalkingPadProtoType proto) {
  switch (state) {
  case WalkingPadBleStateIdle:
    return "Disconnected";
  case WalkingPadBleStateScanning:
    return "Scanning...";
  case WalkingPadBleStateConnecting:
    return "Connecting...";
  case WalkingPadBleStateDiscovering:
    return "Discovering...";
  case WalkingPadBleStateReady:
    return (proto == WalkingPadProtoFTMS) ? "Connected (FTMS)"
                                          : "Connected (WiLink)";
  case WalkingPadBleStateError:
    return "Error";
  default:
    return "???";
  }
}

static void walkingpad_main_view_draw(Canvas *canvas, void *_model) {
  WalkingPadMainViewModel *model = _model;
  bool connected = (model->ble_state == WalkingPadBleStateReady);
  bool busy = (model->ble_state == WalkingPadBleStateScanning ||
               model->ble_state == WalkingPadBleStateConnecting ||
               model->ble_state == WalkingPadBleStateDiscovering);
  bool has_mode = walkingpad_proto_supports_mode(model->proto);

  canvas_clear(canvas);
  canvas_set_font(canvas, FontPrimary);

  /* Title — show protocol when connected */
  const char *title = "WalkingPad";
  if (connected) {
    title = (model->proto == WalkingPadProtoFTMS) ? "WalkingPad (FTMS)"
                                                  : "WalkingPad (WiLink)";
  }
  canvas_draw_str_aligned(canvas, 64, 6, AlignCenter, AlignCenter, title);
  canvas_draw_line(canvas, 0, 12, 128, 12);

  if (!connected) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter,
                            ble_state_str(model->ble_state, model->proto));

    if (!busy) {
      canvas_set_font(canvas, FontKeyboard);
      if (model->selected_item == ItemConnect)
        canvas_invert_color(canvas);
      canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter,
                              "Scan (OK)");
      if (model->selected_item == ItemConnect)
        canvas_invert_color(canvas);
    }
  } else {
    canvas_set_font(canvas, FontSecondary);
    FuriString *tmp = furi_string_alloc();

    furi_string_printf(tmp, "Spd: %.1f km/h  Tgt: %.1f",
                       (double)model->status.speed_kmh,
                       (double)(model->target_speed_tenths / 10.0f));
    canvas_draw_str(canvas, 2, 22, furi_string_get_cstr(tmp));

    if (has_mode) {
      furi_string_printf(tmp, "Mode: %s",
                         model->status.mode == WalkingPadModeManual ? "Manual"
                         : model->status.mode == WalkingPadModeAuto ? "Auto"
                         : model->status.mode == WalkingPadModeStandby
                             ? "Standby"
                             : "???");
    } else {
      furi_string_printf(tmp, "Cal: %u kcal", model->status.calories);
    }
    canvas_draw_str(canvas, 2, 32, furi_string_get_cstr(tmp));

    uint32_t t = model->status.time_seconds;
    furi_string_printf(tmp, "Time: %02lu:%02lu:%02lu", t / 3600,
                       (t % 3600) / 60, t % 60);
    canvas_draw_str(canvas, 2, 42, furi_string_get_cstr(tmp));

    furi_string_printf(tmp, "Dist: %.2f km  Steps: %lu",
                       (double)model->status.distance_km, model->status.steps);
    canvas_draw_str(canvas, 2, 52, furi_string_get_cstr(tmp));

    furi_string_free(tmp);
  }

  /* Bottom action bar */
  canvas_set_font(canvas, FontKeyboard);
  const char *action = "";
  switch (model->selected_item) {
  case ItemConnect:
    action = connected ? "Connected" : (busy ? "..." : "Scan");
    break;
  case ItemDisconnect:
    action = "Disconnect";
    break;
  case ItemStart:
    action = "Start Belt";
    break;
  case ItemStop:
    action = "Stop Belt";
    break;
  case ItemSpeedDown:
    action = "Speed Down";
    break;
  case ItemSpeedUp:
    action = "Speed Up";
    break;
  case ItemToggleMode:
    action = has_mode ? "Toggle Mode" : "(N/A)";
    break;
  default:
    break;
  }
  canvas_draw_box(canvas, 0, 51, 128, 13);
  canvas_set_color(canvas, ColorWhite);
  canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignCenter, action);
  canvas_set_color(canvas, ColorBlack);
}

static bool walkingpad_main_view_input(InputEvent *event, void *context) {
  WalkingPadMainView *view = context;
  WalkingPadMainViewModel *model = view_get_model(view->view);
  bool consumed = false;

  if (event->type == InputTypeShort || event->type == InputTypeRepeat) {
    if (event->key == InputKeyUp) {
      model->selected_item =
          (model->selected_item > 0) ? model->selected_item - 1 : ItemCount - 1;
      consumed = true;
    } else if (event->key == InputKeyDown) {
      model->selected_item = (model->selected_item + 1) % ItemCount;
      consumed = true;
    } else if (event->key == InputKeyOk) {
      if (model->cb) {
        WalkingPadMainViewInputResult res =
            WalkingPadMainViewInputResultConnect;
        switch (model->selected_item) {
        case ItemConnect:
          res = WalkingPadMainViewInputResultConnect;
          break;
        case ItemDisconnect:
          res = WalkingPadMainViewInputResultDisconnect;
          break;
        case ItemStart:
          res = WalkingPadMainViewInputResultStart;
          break;
        case ItemStop:
          res = WalkingPadMainViewInputResultStop;
          break;
        case ItemSpeedDown:
          res = WalkingPadMainViewInputResultSpeedDown;
          break;
        case ItemSpeedUp:
          res = WalkingPadMainViewInputResultSpeedUp;
          break;
        case ItemToggleMode:
          res = WalkingPadMainViewInputResultToggleMode;
          break;
        default:
          break;
        }
        model->cb(res, model->cb_ctx);
      }
      consumed = true;
    }
  }

  view_commit_model(view->view, consumed);
  return consumed;
}

WalkingPadMainView *walkingpad_main_view_alloc() {
  WalkingPadMainView *view = malloc(sizeof(WalkingPadMainView));
  view->view = view_alloc();
  view_allocate_model(view->view, ViewModelTypeLocking,
                      sizeof(WalkingPadMainViewModel));

  WalkingPadMainViewModel *model = view_get_model(view->view);
  model->ble_state = WalkingPadBleStateIdle;
  model->proto = WalkingPadProtoUnknown;
  memset(&model->status, 0, sizeof(model->status));
  model->target_speed_tenths = 25;
  model->selected_item = ItemConnect;
  model->cb = NULL;
  model->cb_ctx = NULL;
  view_commit_model(view->view, false);

  view_set_context(view->view, view);
  view_set_draw_callback(view->view, walkingpad_main_view_draw);
  view_set_input_callback(view->view, walkingpad_main_view_input);
  return view;
}

void walkingpad_main_view_free(WalkingPadMainView *view) {
  view_free(view->view);
  free(view);
}

View *walkingpad_main_view_get_view(WalkingPadMainView *view) {
  return view->view;
}

void walkingpad_main_view_update_state(WalkingPadMainView *view,
                                       WalkingPadBleState ble_state,
                                       WalkingPadProtoType proto,
                                       const WalkingPadStatus *status,
                                       uint8_t target_speed_tenths) {
  furi_assert(view);
  WalkingPadMainViewModel *model = view_get_model(view->view);
  model->ble_state = ble_state;
  model->proto = proto;
  if (status) {
    model->status = *status;
  }
  model->target_speed_tenths = target_speed_tenths;
  view_commit_model(view->view, true);
}

void walkingpad_main_view_set_callback(WalkingPadMainView *view,
                                       WalkingPadMainViewCallback cb,
                                       void *context) {
  WalkingPadMainViewModel *model = view_get_model(view->view);
  model->cb = cb;
  model->cb_ctx = context;
  view_commit_model(view->view, false);
}
