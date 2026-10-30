# Wayland touchscreen UI and node editing

This patch adds native touchscreen contact events to the Wayland backend and
recognizes gestures throughout Blender's UI. Two-finger taps produce the native
right-click action in editors, controls, and popups. Two- and three-finger drags
scroll menus, lists, and panels with inertia; node canvases also support pinch
zoom. X11 and Windows touchscreen support is outside this patch.

## Gestures

- Drag with two or three fingers to pan. Spread or pinch to zoom around their centroid.
  Pan and zoom can be combined. Rotation and zoom inertia are disabled.
- Lift after a pan to coast. A new touch, mouse press, popup, or editor change
  stops the motion.
- Tap with two fingers for a right-click at their centroid anywhere in the UI.
  The first finger does not activate a button while waiting for the second.
- Drag with two or three fingers over scrollable menus, lists, or panels to scroll.
  Movement coasts after release using the same decay as canvas panning. UI pinch
  changes do not zoom the controls. Scrolling stays anchored to its initial target
  and respects the native scroll bounds; it does not select menu entries.
- A new touch, mouse press, keyboard input, popup dismissal, or window deactivation
  stops UI scrolling. Closing a popup cannot scroll the editor behind it.
- Hold one finger on empty canvas to open Add. Keep touching and slide over
  categories to open submenus, then release over a node entry to select it.
- The selected node remains a preview. Tap the canvas to place its upper-left
  corner at that point. Two- or three-finger pan/pinch remains available before placement.
- Two-finger tap or Escape cancels pending placement and removes the new node.
- Releasing outside the Add menu cancels it. Releasing over a category keeps its
  submenu open for another touch selection. Existing menu edge scrolling applies.

Single-finger selection, node dragging, socket linking, and controls remain
available. Holding on nodes, frames, links, sockets, or scrollbars does not open
Add. Once a single-finger drag starts, additional contacts cannot turn it into a
canvas gesture. Adding a third finger or lifting from three fingers to two
rebases the gesture without a canvas jump and preserves pan inertia. Once fewer
than two contacts remain, all contacts must lift before a new gesture begins.
Three-finger taps do not open menus or cancel placement. Four or more contacts
suppress navigation until all fingers lift.

## Thresholds

Distances are logical pixels, independent of the window's native pixel scale.

| Behavior | Threshold |
| --- | --- |
| Hold to open Add | 500 ms |
| Movement slop | 12 logical pixels |
| Two-finger tap arrival separation | At most 120 ms |
| Two-finger tap total duration | At most 300 ms |
| Touch submenu dwell | 200 ms |
| Inertia velocity sampling | Final 100 ms of movement |
| Minimum inertia starting speed | 80 logical pixels/second |
| Inertia exponential time constant | 180 ms |
| Inertia stopping speed / duration | 10 logical pixels/second / 1.5 seconds |

Contact cancellation, window deactivation, removal of the touch device, and
window/region destruction clear captured input. Touch event payloads own their
contact data; timers and gesture state are not saved into `.blend` files.
Mouse, trackpad, and pen events do not enter the touch recognizer.

## Building and testing

Build normally, or run `./build_files/build_dlss_linux.sh` for this machine's
existing DLSS configuration and sccache setup.

Recognizer tests are registered with the window-manager test suite when
`WITH_GTESTS=ON`. They can also run independently of Blender's graphics setup:

```sh
c++ -std=c++20 -O1 -pthread \
  -I extern/gtest/include -I extern/gtest \
  source/blender/windowmanager/intern/wm_touch_gestures_test.cc \
  extern/gtest/src/gtest-all.cc extern/gtest/src/gtest_main.cc \
  -o /tmp/blender-touch-tests
/tmp/blender-touch-tests
```

Use `--log 'wm.touch,ghost.wl.handle.touch' --log-level debug` to diagnose native
contact delivery and recognition. Real touchscreen acceptance should check
Geometry, Shader, and Compositor editors, display scaling, menu edges, undo,
interrupted gestures, and mixed touch/mouse/pen input. Synthetic compositor input
checks software delivery but cannot validate a device's firmware or ergonomics.

## Verification on this branch

The full Linux DLSS build passed. Twenty deterministic recognizer/scroll tests passed with
AddressSanitizer and UndefinedBehaviorSanitizer enabled. An isolated KWin virtual
Wayland desktop delivered native touch events through the compositor and GHOST.
Those checks covered hold/slide/placement in Shader, Geometry, and Compositor
editors, exact placement coordinates, one-step undo and redo, two-finger context
menus, pending-preview cancellation, fixed preview position during pan/pinch, and
inertial panning. A 2x output verified logical movement thresholds and ordinary
single-finger node dragging. The final DLSS capability check and OptiX render also
passed. Physical touchscreen and pen hardware acceptance remains outstanding.

## Three-finger support

The two- and three-contact paths share centroid panning and RMS-spread pinch
scaling. For two contacts the spread equals the original pair distance, so the
zoom response is unchanged. Changing contact count rebases the geometry and
velocity samples; it does not emit navigation. Stationary contact-count changes
cannot create momentum. Two-finger tap remains the only context-click gesture.

Use `touch-support` for this combined DLSS and touchscreen build. See
[local-feature-branches.md](local-feature-branches.md) for the separate DLSS branch,
future touch feature branches, upstream updates, and tested snapshot tags.

The three-finger update additionally passed compositor-injected pan, pinch,
inertia, inertia interruption, and two-to-three / three-to-two contact transitions.
Physical touchscreen testing remains outstanding. The support branches preserve
published history and the upstream Blender version number.

## UI-wide touch support

Single-finger taps are dispatched on release, and drags begin after the movement
threshold, so a second finger can turn a pending tap into a right-click without
first toggling a control. Once a single-finger drag starts, extra contacts do not
steal it. Holding empty node canvas still opens the touch Add menu; holding other
UI controls does not open that menu.

Popup and search regions are captured separately from their underlying editor.
Scroll events reuse native smooth-scroll handlers, including UI lists and panel
View2D limits. Fractional pixels accumulate across inertia ticks, and submenu
hover activation is suppressed while touch scrolling. Touch Add menu selection
uses the actual release position, including a fresh tap after lifting on a
category, then restores the originating node canvas for preview placement.

The UI-wide update passed compositor-injected property and 3D View context
clicks, single-finger button activation, panel and popup inertia, popup dismissal
without scroll leaking into the underlying editor, embedded-list scrolling with
three fingers, and node placement after lifting on a submenu category. Scrolling
did not change the test controls' values or execute menu entries. Twenty
sanitized recognizer/scroll tests and the full DLSS build passed.
