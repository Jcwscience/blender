/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "wm_touch_gestures.hh"
#include <gtest/gtest.h>

namespace blender::wm::touch::tests {

TEST(Touch, ContextTapDefersAllLeftButtonActions)
{
  /* Used by buttons, panels, menus, and canvases: the first contact must not
   * toggle a control before the second finger turns the sequence into RMB. */
  Recognizer r;
  EXPECT_TRUE(r.input(Phase::Down, 0, {100, 100}, 0, false).empty());
  EXPECT_TRUE(r.input(Phase::Motion, 0, {103, 100}, 0.02).empty());
  EXPECT_TRUE(r.input(Phase::Down, 1, {120, 100}, 0.04, false).empty());
  EXPECT_TRUE(r.tick(0.1).empty());
  EXPECT_TRUE(r.input(Phase::Up, 0, {103, 100}, 0.15).empty());
  const auto actions = r.input(Phase::Up, 1, {120, 100}, 0.18);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_EQ(actions[0].type, ActionType::RightClick);
}

TEST(Touch, UIContactCancelCannotBecomeClick)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0, false);
  r.input(Phase::Down, 1, {100, 0}, 0.01, false);
  EXPECT_TRUE(r.input(Phase::Cancel, 0, {}, 0.02).empty());
  EXPECT_TRUE(r.input(Phase::Up, 0, {}, 0.03).empty());
  EXPECT_TRUE(r.input(Phase::Up, 1, {100, 0}, 0.03).empty());
  EXPECT_FALSE(r.inertial());
}

TEST(Touch, SlowScrollAccumulatesFractionalPixelsAtNativeScale)
{
  ScrollAccumulator scroll;
  int total = 0;
  for (int i = 0; i < 100; i++) {
    auto [x, y] = scroll.consume({0, 0.1}, 2);
    EXPECT_EQ(x, 0);
    total += y;
  }
  EXPECT_EQ(total, 20);
  for (int i = 0; i < 100; i++) {
    total += scroll.consume({0, -0.1}, 2).second;
  }
  EXPECT_EQ(total, 0);
}

TEST(Touch, NewScrollTargetDoesNotInheritFractionalMovement)
{
  ScrollAccumulator scroll;
  EXPECT_EQ(scroll.consume({0, 0.4}, 1).second, 0);
  scroll.reset();
  EXPECT_EQ(scroll.consume({0, 0.4}, 1).second, 0);
  EXPECT_EQ(scroll.consume({0, 0.4}, 1).second, 1);
}

TEST(Touch, TapAndHoldAreExclusive)
{
  Recognizer r;
  EXPECT_TRUE(r.input(Phase::Down, 7, {100, 100}, 0, true).empty());
  EXPECT_TRUE(r.tick(0.499).empty());
  auto actions = r.input(Phase::Up, 7, {100, 100}, 0.499);
  ASSERT_EQ(actions.size(), 3);
  EXPECT_EQ(actions[1].type, ActionType::Press);
  EXPECT_EQ(actions[2].type, ActionType::Release);
  EXPECT_TRUE(r.tick(1).empty());
  r.input(Phase::Down, 7, {100, 100}, 2, true);
  actions = r.tick(2.501);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_EQ(actions[0].type, ActionType::Hold);
  EXPECT_TRUE(r.tick(2.8).empty());
  actions = r.input(Phase::Up, 7, {150, 150}, 3);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_EQ(actions[0].type, ActionType::Release);
}

TEST(Touch, HoldOnlyWhenAllowedAndMotionCancelsHold)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0, false);
  EXPECT_TRUE(r.tick(1).empty());
  r.cancel();
  r.input(Phase::Down, 0, {}, 0, true);
  auto actions = r.input(Phase::Motion, 0, {13, 0}, 0.2);
  ASSERT_EQ(actions.size(), 3);
  EXPECT_EQ(actions[1].type, ActionType::Press);
  EXPECT_TRUE(r.tick(1).empty());
}

TEST(Touch, TwoFingerTapDoesNotLeftClick)
{
  Recognizer r;
  EXPECT_TRUE(r.input(Phase::Down, 9, {0, 0}, 0).empty());
  EXPECT_TRUE(r.input(Phase::Down, 2, {100, 0}, 0.100).empty());
  EXPECT_TRUE(r.tick(0.150).empty());
  EXPECT_TRUE(r.input(Phase::Up, 9, {0, 0}, 0.200).empty());
  auto actions = r.input(Phase::Up, 2, {100, 0}, 0.250);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_EQ(actions[0].type, ActionType::RightClick);
  EXPECT_DOUBLE_EQ(actions[0].position.x, 50);
  EXPECT_FALSE(r.inertial());
}

TEST(Touch, LateSecondContactAndSlowTapDoNotRightClick)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0);
  r.input(Phase::Down, 1, {100, 0}, 0.121);
  EXPECT_TRUE(r.input(Phase::Up, 0, {}, 0.2).empty());
  EXPECT_TRUE(r.input(Phase::Up, 1, {100, 0}, 0.25).empty());
  r.input(Phase::Down, 0, {}, 1);
  r.input(Phase::Down, 1, {100, 0}, 1.05);
  r.input(Phase::Up, 0, {}, 1.20);
  EXPECT_TRUE(r.input(Phase::Up, 1, {100, 0}, 1.301).empty());
}

TEST(Touch, PanAndPinchUseCentroidAndDistance)
{
  Recognizer r;
  r.input(Phase::Down, 0, {0, 0}, 0);
  r.input(Phase::Down, 1, {100, 0}, 0.01);
  auto actions = r.input(Phase::Motion, 1, {200, 0}, 0.02);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_EQ(actions[0].type, ActionType::Navigate);
  EXPECT_DOUBLE_EQ(actions[0].previous.x, 50);
  EXPECT_DOUBLE_EQ(actions[0].position.x, 100);
  EXPECT_DOUBLE_EQ(actions[0].zoom, 2);
  r.input(Phase::Up, 0, {}, 0.03);
  EXPECT_TRUE(r.input(Phase::Motion, 1, {500, 0}, 0.04).empty());
}

TEST(Touch, InertiaDecaysAndStopsOnNewContact)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0);
  r.input(Phase::Down, 1, {100, 0}, 0.01);
  r.input(Phase::Motion, 0, {20, 0}, 0.02);
  r.input(Phase::Motion, 1, {120, 0}, 0.03);
  r.input(Phase::Up, 0, {20, 0}, 0.04);
  r.input(Phase::Up, 1, {120, 0}, 0.04);
  ASSERT_TRUE(r.inertial());
  auto a = r.tick(0.05);
  auto b = r.tick(0.06);
  ASSERT_EQ(a.size(), 1);
  ASSERT_EQ(b.size(), 1);
  EXPECT_GT((a[0].position - a[0].previous).length(), (b[0].position - b[0].previous).length());
  Recognizer continuing = r;
  continuing.tick(2);
  EXPECT_FALSE(continuing.inertial());
  r.input(Phase::Down, 5, {}, 0.07);
  EXPECT_FALSE(r.inertial());
}

TEST(Touch, NoInertiaAfterPausingBeforeRelease)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0);
  r.input(Phase::Down, 1, {100, 0}, 0.01);
  r.input(Phase::Motion, 0, {20, 0}, 0.02);
  r.input(Phase::Motion, 1, {120, 0}, 0.03);
  r.input(Phase::Up, 0, {20, 0}, 1);
  r.input(Phase::Up, 1, {120, 0}, 1);
  EXPECT_FALSE(r.inertial());
}

TEST(Touch, ThreeFingerPanUsesAllContacts)
{
  Recognizer r;
  r.input(Phase::Down, 9, {0, 0}, 0);
  r.input(Phase::Down, 2, {100, 0}, 0.01);
  EXPECT_TRUE(r.input(Phase::Down, 7, {50, 90}, 0.02).empty());
  auto actions = r.input(Phase::Motion, 7, {80, 120}, 0.03);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_EQ(actions[0].type, ActionType::Navigate);
  EXPECT_DOUBLE_EQ(actions[0].previous.x, 50);
  EXPECT_DOUBLE_EQ(actions[0].previous.y, 30);
  EXPECT_DOUBLE_EQ(actions[0].position.x, 60);
  EXPECT_DOUBLE_EQ(actions[0].position.y, 40);
}

TEST(Touch, ThreeFingerPinchUsesSpreadAndNeverRightClicks)
{
  Recognizer r;
  r.input(Phase::Down, 0, {-100, 0}, 0);
  r.input(Phase::Down, 1, {0, 0}, 0.01);
  r.input(Phase::Down, 2, {100, 0}, 0.02);
  auto first = r.input(Phase::Motion, 0, {-200, 0}, 0.03);
  auto second = r.input(Phase::Motion, 2, {200, 0}, 0.04);
  ASSERT_EQ(first.size(), 1);
  ASSERT_EQ(second.size(), 1);
  EXPECT_NEAR(first[0].zoom * second[0].zoom, 2, 1e-12);
  EXPECT_DOUBLE_EQ(second[0].position.x, 0);
  EXPECT_TRUE(r.input(Phase::Up, 0, {-200, 0}, 0.05).empty());
  EXPECT_TRUE(r.input(Phase::Up, 1, {}, 0.05).empty());
  EXPECT_TRUE(r.input(Phase::Up, 2, {200, 0}, 0.05).empty());
}

TEST(Touch, ThreeFingerTapAndMotionWithinSlopDoNothing)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0, true);
  r.input(Phase::Down, 1, {100, 0}, 0.01);
  r.input(Phase::Down, 2, {50, 100}, 0.02);
  EXPECT_TRUE(r.input(Phase::Motion, 2, {55, 100}, 0.03).empty());
  EXPECT_TRUE(r.tick(0.6).empty());
  EXPECT_TRUE(r.input(Phase::Up, 2, {55, 100}, 0.61).empty());
  EXPECT_TRUE(r.input(Phase::Up, 0, {}, 0.62).empty());
  EXPECT_TRUE(r.input(Phase::Up, 1, {100, 0}, 0.63).empty());
  EXPECT_FALSE(r.inertial());
  r.input(Phase::Down, 0, {}, 1);
  r.input(Phase::Down, 1, {100, 0}, 1.01);
  r.input(Phase::Down, 2, {50, 100}, 1.02);
  r.input(Phase::Up, 2, {50, 100}, 1.03);
  r.input(Phase::Up, 0, {}, 1.04);
  EXPECT_TRUE(r.input(Phase::Up, 1, {100, 0}, 1.05).empty());
}

TEST(Touch, ContactCountChangesRebaseWithoutMovingOrZooming)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0);
  r.input(Phase::Down, 1, {100, 0}, 0.01);
  r.input(Phase::Motion, 0, {20, 0}, 0.02);
  r.input(Phase::Motion, 1, {120, 0}, 0.03);
  EXPECT_TRUE(r.input(Phase::Down, 2, {1000, 0}, 0.04).empty());
  auto same = r.input(Phase::Motion, 2, {1000, 0}, 0.05);
  ASSERT_EQ(same.size(), 1);
  EXPECT_DOUBLE_EQ(same[0].position.x, same[0].previous.x);
  EXPECT_DOUBLE_EQ(same[0].zoom, 1);
  EXPECT_TRUE(r.input(Phase::Up, 2, {1000, 0}, 0.06).empty());
  same = r.input(Phase::Motion, 1, {120, 0}, 0.07);
  ASSERT_EQ(same.size(), 1);
  EXPECT_DOUBLE_EQ(same[0].position.x, same[0].previous.x);
  EXPECT_DOUBLE_EQ(same[0].zoom, 1);
  auto move = r.input(Phase::Motion, 0, {40, 0}, 0.08);
  ASSERT_EQ(move.size(), 1);
  EXPECT_DOUBLE_EQ(move[0].position.x - move[0].previous.x, 10);
}

TEST(Touch, ThreeFingerPanRetainsInertiaOnSimultaneousRelease)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0);
  r.input(Phase::Down, 1, {100, 0}, 0.01);
  r.input(Phase::Down, 2, {50, 100}, 0.02);
  r.input(Phase::Motion, 0, {30, 0}, 0.03);
  r.input(Phase::Motion, 1, {130, 0}, 0.03);
  r.input(Phase::Motion, 2, {80, 100}, 0.03);
  r.input(Phase::Up, 2, {80, 100}, 0.04);
  r.input(Phase::Up, 0, {30, 0}, 0.04);
  r.input(Phase::Up, 1, {130, 0}, 0.04);
  ASSERT_TRUE(r.inertial());
  auto actions = r.tick(0.05);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_GT(actions[0].position.x, actions[0].previous.x);
  r.input(Phase::Down, 4, {}, 0.06);
  EXPECT_FALSE(r.inertial());
}

TEST(Touch, StationaryContactChangesDoNotCreateInertia)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0);
  r.input(Phase::Down, 1, {100, 0}, 0.01);
  r.input(Phase::Down, 2, {10000, 10000}, 0.02);
  r.input(Phase::Up, 2, {10000, 10000}, 0.03);
  r.input(Phase::Up, 0, {}, 0.04);
  r.input(Phase::Up, 1, {100, 0}, 0.04);
  EXPECT_FALSE(r.inertial());
  EXPECT_TRUE(r.tick(0.05).empty());
}

TEST(Touch, FourthContactAndReplacementSuppressNavigationAndTap)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0);
  r.input(Phase::Down, 1, {100, 0}, 0.01);
  r.input(Phase::Down, 2, {200, 0}, 0.02);
  r.input(Phase::Down, 3, {300, 0}, 0.03);
  EXPECT_TRUE(r.input(Phase::Motion, 0, {20, 0}, 0.04).empty());
  EXPECT_TRUE(r.input(Phase::Up, 3, {}, 0.05).empty());
  EXPECT_TRUE(r.input(Phase::Motion, 0, {40, 0}, 0.06).empty());
  EXPECT_TRUE(r.input(Phase::Up, 0, {}, 0.07).empty());
  EXPECT_TRUE(r.input(Phase::Up, 1, {}, 0.08).empty());
  EXPECT_TRUE(r.input(Phase::Up, 2, {}, 0.09).empty());
  EXPECT_FALSE(r.inertial());
  r.input(Phase::Down, 0, {}, 1);
  r.input(Phase::Down, 1, {100, 0}, 1.01);
  r.input(Phase::Up, 0, {}, 1.02);
  r.input(Phase::Down, 2, {200, 0}, 1.03);
  r.input(Phase::Up, 1, {}, 1.04);
  EXPECT_TRUE(r.input(Phase::Up, 2, {}, 1.05).empty());
}

TEST(Touch, ExtraContactsCannotStealSingleFingerDrag)
{
  Recognizer r;
  r.input(Phase::Down, 9, {}, 0);
  r.input(Phase::Motion, 9, {20, 0}, 0.01);
  r.input(Phase::Down, 2, {100, 0}, 0.02);
  EXPECT_TRUE(r.input(Phase::Up, 2, {100, 0}, 0.03).empty());
  auto actions = r.input(Phase::Up, 9, {20, 0}, 0.04);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_EQ(actions[0].type, ActionType::Release);
}

TEST(Touch, CancelClearsHeldAndDraggedSequences)
{
  Recognizer r;
  r.input(Phase::Down, 0, {}, 0, true);
  r.tick(1);
  auto actions = r.input(Phase::Cancel, 0, {}, 1.1);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_EQ(actions[0].type, ActionType::Cancel);
  EXPECT_TRUE(r.empty());
  EXPECT_FALSE(r.inertial());
  EXPECT_TRUE(r.input(Phase::Motion, 0, {100, 100}, 2).empty());
  EXPECT_TRUE(r.input(Phase::Up, 0, {}, 3).empty());
  r.input(Phase::Down, 5, {}, 4);
  r.input(Phase::Motion, 5, {20, 0}, 4.1);
  EXPECT_EQ(r.input(Phase::Cancel, 0, {}, 4.2)[0].type, ActionType::Cancel);
}
}  // namespace blender::wm::touch::tests
