/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "GHOST_Event.hh"

class GHOST_EventTouch : public GHOST_Event {
  GHOST_TEventTouchData touch_;

 public:
  GHOST_EventTouch(uint64_t time, GHOST_IWindow *window, GHOST_TEventTouchData touch)
      : GHOST_Event(time, GHOST_kEventTouch, window), touch_(touch)
  {
    data_ = &touch_;
  }
};
