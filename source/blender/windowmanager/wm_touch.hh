/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <cstdint>
namespace blender {
struct bContext;
struct wmEvent;
struct wmWindow;
enum wmEventType : int16_t;
/** Consumes native contacts and the private touch timer before modal dispatch. */
bool wm_touch_event_handle(bContext *C, wmWindow *win, const wmEvent *event);
/** Called only when the touch Add menu executes a node entry. */
void wm_touch_node_placement_begin(bContext *C);
bool wm_touch_node_placement_active(const wmWindow *win);
bool wm_touch_menu_active(const wmWindow *win);
bool wm_touch_ui_scrolling(const wmWindow *win);
void wm_touch_menu_end(bContext *C);
wmEvent *wm_touch_mouse_event_add(wmWindow *win,
                                  wmEvent *after,
                                  wmEventType type,
                                  short value,
                                  int x,
                                  int y,
                                  double time,
                                  bool menu);
}  // namespace blender
