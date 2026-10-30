/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "wm_touch.hh"
#include "GHOST_IWindow.hh"
#include "wm_event_types.hh"
#include "wm_touch_gestures.hh"

#include "BKE_context.hh"
#include "BKE_screen.hh"
#include "BKE_wm_runtime.hh"
#include "BLI_listbase.hh"
#include "BLI_rect.hh"
#include "BLI_time.hh"
#include "CLG_log.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_view2d_types.h"
#include "ED_node.hh"
#include "ED_screen.hh"
#include "UI_interface_c.hh"
#include "UI_view2d.hh"
#include "WM_api.hh"
#include "WM_types.hh"

namespace blender {
static CLG_LogRef LOG = {"wm.touch"};

using wm::touch::Action;
using wm::touch::ActionType;
using wm::touch::Phase;
using wm::touch::Point;

struct wmTouchState {
  wm::touch::Recognizer gestures;
  bScreen *screen = nullptr;
  ScrArea *area = nullptr;
  ARegion *region = nullptr;
  wmTimer *timer = nullptr;
  int area_type = SPACE_EMPTY;
  ScrArea *node_area = nullptr;
  ARegion *node_region = nullptr;
  bool popup = false;
  bool ui_scroll = false;
  bool scrolling = false;
  Point scroll_anchor;
  wm::touch::ScrollAccumulator scroll_accumulator;
  std::map<int, Point> contacts;
  bool node_canvas = false;
  bool menu = false;
  bool placement = false;
  bool pointer_down = false;
  double clock_offset = 0;
  double scale = 1;
  Point hold;
};

bool wm_touch_node_placement_active(const wmWindow *win)
{
  return win && win->runtime->touch && win->runtime->touch->placement;
}
bool wm_touch_menu_active(const wmWindow *win)
{
  return win && win->runtime->touch && win->runtime->touch->menu;
}
bool wm_touch_ui_scrolling(const wmWindow *win)
{
  return win && win->runtime->touch && win->runtime->touch->scrolling;
}
void wm_touch_menu_end(bContext *C)
{
  wmWindow *win = CTX_wm_window(C);
  if (win && win->runtime->touch) {
    win->runtime->touch->menu = false;
  }
}

static bool target_valid(wmWindow *win, const wmTouchState &s)
{
  bScreen *screen = WM_window_get_active_screen(win);
  if (screen != s.screen || !screen) {
    return false;
  }
  if (s.popup) {
    for (ARegion &region : screen->regionbase) {
      if (&region == s.region) {
        return true;
      }
    }
    return false;
  }
  if (!s.area) {
    return s.region == nullptr;
  }
  auto area_valid = [&](const auto &areas) {
    for (const ScrArea &area : areas) {
      if (&area == s.area && area.spacetype == s.area_type) {
        for (const ARegion &region : area.regionbase) {
          if (&region == s.region) {
            return true;
          }
        }
        return s.region == nullptr;
      }
    }
    return false;
  };
  return area_valid(screen->areabase) || area_valid(win->global_areas.areabase);
}

static void navigate(wmTouchState &s, const Action &action)
{
  View2D &v = s.region->v2d;
  const Point before = action.previous * s.scale;
  const Point after = action.position * s.scale;
  const double width = BLI_rcti_size_x(&v.mask) + 1;
  const double height = BLI_rcti_size_y(&v.mask) + 1;
  if (width <= 0 || height <= 0) {
    return;
  }
  const double fx = (before.x - s.region->winrct.xmin - v.mask.xmin) / width;
  const double fy = (before.y - s.region->winrct.ymin - v.mask.ymin) / height;
  const double anchor_x = v.cur.xmin + fx * BLI_rctf_size_x(&v.cur);
  const double anchor_y = v.cur.ymin + fy * BLI_rctf_size_y(&v.cur);
  const double zoom = std::clamp(action.zoom, 0.1, 10.0);
  BLI_rctf_resize(&v.cur, BLI_rctf_size_x(&v.cur) / zoom, BLI_rctf_size_y(&v.cur) / zoom);
  ui::view2d_curRect_validate(&v);
  const double new_fx = (after.x - s.region->winrct.xmin - v.mask.xmin) / width;
  const double new_fy = (after.y - s.region->winrct.ymin - v.mask.ymin) / height;
  BLI_rctf_translate(&v.cur,
                     anchor_x - (v.cur.xmin + new_fx * BLI_rctf_size_x(&v.cur)),
                     anchor_y - (v.cur.ymin + new_fy * BLI_rctf_size_y(&v.cur)));
  ui::view2d_curRect_validate(&v);
  ED_region_tag_redraw(s.region);
}

static wmEvent *emit(wmWindow *win,
                     wmTouchState &s,
                     wmEvent *after,
                     wmEventType type,
                     short value,
                     Point point,
                     double time)
{
  return wm_touch_mouse_event_add(win,
                                  after,
                                  type,
                                  value,
                                  std::lround(point.x * s.scale),
                                  std::lround(point.y * s.scale),
                                  time,
                                  s.menu);
}

static wmEvent *scroll(
    wmWindow *win, wmTouchState &s, wmEvent *after, const Action &action, double time)
{
  s.scrolling = true;
  const auto [dx, dy] = s.scroll_accumulator.consume(action.position - action.previous, s.scale);
  if (dx == 0 && dy == 0) {
    return after;
  }
  /* Keep the event over the captured widget even when coasting would move the
   * virtual centroid outside it. Native UI handlers supply bounds and list rules. */
  wmEvent *pan = emit(win, s, after, MOUSEPAN, KM_NOTHING, s.scroll_anchor, time);
  pan->prev_xy[0] = pan->xy[0] - dx;
  pan->prev_xy[1] = pan->xy[1] - dy;
  pan->modifier = wmEventModifierFlag(0);
  return pan;
}

static void dispatch(bContext *C,
                     wmWindow *win,
                     wmTouchState &s,
                     const wmEvent *event,
                     const std::vector<Action> &actions,
                     double time)
{
  wmEvent *after = const_cast<wmEvent *>(event);
  for (const Action &action : actions) {
    switch (action.type) {
      case ActionType::Move:
        after = emit(win, s, after, MOUSEMOVE, KM_NOTHING, action.position, time);
        break;
      case ActionType::Press:
        /* Confirm touch placement on release, after the exact-coordinate motion. */
        if (!s.placement && !s.menu) {
          after = emit(win, s, after, LEFTMOUSE, KM_PRESS, action.position, time);
          s.pointer_down = true;
        }
        break;
      case ActionType::Release:
        if (s.placement) {
          after = emit(win, s, after, LEFTMOUSE, KM_PRESS, action.position, time);
          s.placement = false;
        }
        after = emit(win, s, after, LEFTMOUSE, KM_RELEASE, action.position, time);
        s.pointer_down = false;
        break;
      case ActionType::RightClick:
        after = emit(win, s, after, MOUSEMOVE, KM_NOTHING, action.position, time);
        if (s.placement) {
          after = emit(win, s, after, EVT_ESCKEY, KM_PRESS, action.position, time);
          s.placement = false;
        }
        else {
          after = emit(win, s, after, RIGHTMOUSE, KM_PRESS, action.position, time);
          after = emit(win, s, after, RIGHTMOUSE, KM_RELEASE, action.position, time);
        }
        break;
      case ActionType::Hold: {
        CLOG_DEBUG(&LOG, "opening touch Add menu");
        s.hold = action.position;
        s.node_area = s.area;
        s.node_region = s.region;
        s.menu = true;
        s.gestures.stop_inertia();
        const int x = std::lround(s.hold.x * s.scale), y = std::lround(s.hold.y * s.scale);
        win->runtime->eventstate->xy[0] = x;
        win->runtime->eventstate->xy[1] = y;
        CTX_wm_area_set(C, s.area);
        CTX_wm_region_set(C, s.region);
        WM_menu_name_call(C, "NODE_MT_add", wm::OpCallContext::InvokeRegionWin);
        after = emit(win, s, after, MOUSEMOVE, KM_NOTHING, action.position, time);
        break;
      }
      case ActionType::Navigate:
        if (s.node_canvas) {
          navigate(s, action);
        }
        else if (s.ui_scroll) {
          after = scroll(win, s, after, action, time);
        }
        else {
          s.gestures.stop_inertia();
        }
        break;
      case ActionType::Cancel:
        after = emit(win, s, after, EVT_ESCKEY, KM_PRESS, action.position, time);
        if (s.pointer_down) {
          after = emit(win, s, after, LEFTMOUSE, KM_RELEASE, action.position, time);
          s.pointer_down = false;
        }
        s.menu = s.placement = s.scrolling = false;
        break;
    }
  }
}

void wm_touch_node_placement_begin(bContext *C)
{
  wmWindow *win = CTX_wm_window(C);
  if (!win || !win->runtime->touch) {
    return;
  }
  wmTouchState &s = *win->runtime->touch;
  s.menu = false;
  s.placement = true;
  s.area = s.node_area;
  s.region = s.node_region;
  s.area_type = SPACE_NODE;
  s.node_canvas = true;
  s.popup = s.ui_scroll = s.scrolling = false;
  /* The add operator uses this position as its invocation origin. */
  win->runtime->eventstate->xy[0] = std::lround(s.hold.x * s.scale);
  win->runtime->eventstate->xy[1] = std::lround(s.hold.y * s.scale);
  /* After the deferred add callback starts its transform, anchor the preview at the hold. */
  emit(win,
       s,
       win->runtime->event_queue.first(),
       MOUSEMOVE,
       KM_NOTHING,
       s.hold,
       BLI_time_now_seconds() - s.clock_offset);
}

bool wm_touch_event_handle(bContext *C, wmWindow *win, const wmEvent *event)
{
  if (!win->runtime->touch && event->type != EVT_TOUCH) {
    return false;
  }
  if (!win->runtime->touch) {
    win->runtime->touch = std::make_shared<wmTouchState>();
  }
  wmTouchState &s = *win->runtime->touch;
  wmWindowManager *wm = CTX_wm_manager(C);
  const bool timer = s.timer && event->type == TIMER && event->customdata == s.timer;
  const bool native = event->type == EVT_TOUCH;
  const bool touch_pan = event->type == MOUSEPAN && (event->flag & WM_EVENT_TOUCH);
  const bool captured = !s.gestures.empty() || s.gestures.inertial() || s.menu || s.placement;
  const bool invalid = (captured || timer || touch_pan) && !target_valid(win, s);
  if (invalid || event->type == WINDEACTIVATE) {
    std::vector<Action> cancel = s.gestures.input(Phase::Cancel, 0, {}, 0);
    if (s.placement || s.menu) {
      cancel.push_back({ActionType::Cancel, s.hold, {}});
    }
    dispatch(C, win, s, event, cancel, 0);
    s.contacts.clear();
    s.node_canvas = s.menu = s.placement = s.scrolling = false;
    if (s.timer) {
      WM_event_timer_remove(wm, win, s.timer);
      s.timer = nullptr;
    }
    return native || timer || touch_pan;
  }
  if (!native && !timer) {
    if (!(event->flag & WM_EVENT_TOUCH) && event->val == KM_PRESS &&
        (ISMOUSE_BUTTON(event->type) || ISKEYBOARD(event->type)))
    {
      s.gestures.stop_inertia();
      s.scrolling = false;
      if (ISMOUSE_BUTTON(event->type) || event->type == EVT_ESCKEY) {
        s.menu = s.placement = false;
      }
    }
    return false;
  }
  if (timer) {
    if (!s.popup && !s.menu && ui::popup_block_exists(s.screen)) {
      s.gestures.stop_inertia();
    }
    dispatch(C,
             win,
             s,
             event,
             s.gestures.tick(BLI_time_now_seconds() - s.clock_offset),
             BLI_time_now_seconds() - s.clock_offset);
    if (s.gestures.empty() && !s.gestures.inertial()) {
      WM_event_timer_remove(wm, win, s.timer);
      s.timer = nullptr;
      s.scrolling = false;
    }
    return true;
  }
  const Phase phase = Phase(event->touch_phase);
  const double time = event->touch_time;
  s.clock_offset = BLI_time_now_seconds() - time;
  if (phase == Phase::Down && s.contacts.empty()) {
    s.gestures.stop_inertia();
    s.scrolling = false;
    s.scroll_accumulator.reset();
    if (s.menu && !ui::popup_block_exists(WM_window_get_active_screen(win))) {
      s.menu = false;
    }
    if (!s.placement) {
      s.area = CTX_wm_area(C);
      s.region = CTX_wm_region(C);
      s.screen = WM_window_get_active_screen(win);
      s.area_type = s.area ? s.area->spacetype : SPACE_EMPTY;
      ARegion *popup = ui::popup_region_find_xy(s.screen, event->xy);
      s.popup = popup != nullptr;
      if (popup) {
        s.region = popup;
      }
      s.node_canvas = !s.popup && s.area && s.area->spacetype == SPACE_NODE && s.region &&
                      s.region->regiontype == RGN_TYPE_WINDOW && !ui::popup_block_exists(s.screen);
      s.ui_scroll = !s.node_canvas && s.region &&
                    (s.popup ||
                     (s.region->v2d.scroll & (V2D_SCROLL_VERTICAL | V2D_SCROLL_HORIZONTAL)) ||
                     ELEM(s.region->regiontype, RGN_TYPE_UI, RGN_TYPE_TOOLS));
    }
    auto *ghost = static_cast<GHOST_IWindow *>(win->runtime->ghostwin);
    /* Wayland already scales GHOST coordinates into framebuffer pixels. Its
     * native-pixel multiplier remains 1; the DPI hint carries the surface scale. */
    s.scale = ghost ? std::max(0.1, double(ghost->getDPIHint()) / 96.0) : 1.0;
    s.scroll_anchor = {event->xy[0] / s.scale, event->xy[1] / s.scale};
  }
  Point p{event->xy[0] / s.scale, event->xy[1] / s.scale};
  if (phase == Phase::Down) {
    s.contacts[event->touch_id] = p;
  }
  CLOG_DEBUG(&LOG, "node_canvas=%d menu=%d placement=%d", s.node_canvas, s.menu, s.placement);
  const bool hold = s.node_canvas && phase == Phase::Down && !s.menu && !s.placement &&
                    ed::space_node::touch_is_empty_canvas(C, *s.region, event->xy);
  CLOG_DEBUG(&LOG,
             "contact phase=%d id=%d xy=%g,%g hold=%d",
             int(phase),
             event->touch_id,
             p.x,
             p.y,
             hold);
  const double hold_duration = std::clamp(int(U.touch_hold_time), 200, 5000) / 1000.0;
  auto actions = s.gestures.input(phase, event->touch_id, p, time, hold, hold_duration);
  dispatch(C, win, s, event, actions, time);
  if (!s.node_canvas && !s.ui_scroll) {
    s.gestures.stop_inertia();
  }
  if (!s.timer && ((!s.gestures.empty() && s.node_canvas) || s.gestures.inertial())) {
    s.timer = WM_event_timer_add(wm, win, TIMER, 1.0 / 60.0);
  }
  if (s.gestures.empty() && !s.gestures.inertial()) {
    s.scrolling = false;
  }
  if (phase == Phase::Up) {
    s.contacts.erase(event->touch_id);
  }
  if (phase == Phase::Cancel) {
    if (s.placement || s.menu) {
      dispatch(C, win, s, event, {{ActionType::Cancel, s.hold, {}}}, time);
    }
    s.contacts.clear();
  }
  return true;
}
}  // namespace blender
