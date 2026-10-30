/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <utility>
#include <vector>

namespace blender::wm::touch {

/* Coordinates and velocities are in logical pixels; times are monotonic seconds. */
struct Point {
  double x = 0, y = 0;
  Point operator+(Point b) const
  {
    return {x + b.x, y + b.y};
  }
  Point operator-(Point b) const
  {
    return {x - b.x, y - b.y};
  }
  Point operator*(double f) const
  {
    return {x * f, y * f};
  }
  double length() const
  {
    return std::hypot(x, y);
  }
};
enum class Phase { Down, Motion, Up, Cancel };
enum class ActionType { Move, Press, Release, RightClick, Hold, Navigate, Cancel };
struct Action {
  ActionType type;
  Point position;
  Point previous;
  double zoom = 1;
};

/* Preserve fractional framebuffer pixels across slow inertial scroll ticks. */
class ScrollAccumulator {
  Point remainder_;

 public:
  std::pair<int, int> consume(Point delta, double scale)
  {
    remainder_ = remainder_ + delta * scale;
    const int x = std::lround(remainder_.x), y = std::lround(remainder_.y);
    remainder_ = remainder_ - Point{double(x), double(y)};
    return {x, y};
  }
  void reset()
  {
    remainder_ = {};
  }
};

class Recognizer {
  struct Contact {
    Point start, position;
  };
  std::map<int, Contact> contacts_;
  std::deque<std::pair<double, Point>> history_;
  double start_time_ = 0, release_time_ = 0, tick_time_ = 0;
  int primary_ = -1;
  Point origin_, center_, velocity_;
  double distance_ = 0;
  bool drag_ = false, held_ = false, multi_ = false, draining_ = false;
  bool tap_ = false, moved_ = false, navigated_ = false, allow_hold_ = false;
  bool inertia_ = false;

  /* Twice the RMS radius equals the original pair distance for two contacts,
   * and gives three contacts the same scale-independent pinch mechanics. */
  std::pair<Point, double> geometry() const
  {
    Point center;
    for (const auto &[id, contact] : contacts_) {
      center = center + contact.position;
    }
    center = center * (1.0 / contacts_.size());
    double radius_squared = 0;
    for (const auto &[id, contact] : contacts_) {
      const Point offset = contact.position - center;
      radius_squared += offset.x * offset.x + offset.y * offset.y;
    }
    return {center, 2 * std::sqrt(radius_squared / contacts_.size())};
  }

  void record_center(double time)
  {
    history_.push_back({time, center_});
    while (history_.size() > 2 && history_[1].first < time - 0.100) {
      history_.pop_front();
    }
  }

  void rebase_geometry(double time)
  {
    const auto [center, distance] = geometry();
    /* Adding/removing a finger must neither move the view nor create velocity.
     * Translate the samples together to retain genuine motion for inertia. */
    const Point shift = center - center_;
    for (auto &sample : history_) {
      sample.second = sample.second + shift;
    }
    center_ = center;
    distance_ = distance;
    record_center(time);
  }

 public:
  bool empty() const
  {
    return contacts_.empty();
  }
  bool inertial() const
  {
    return inertia_;
  }
  void stop_inertia()
  {
    inertia_ = false;
    velocity_ = {};
  }
  void cancel()
  {
    *this = {};
  }

  std::vector<Action> input(Phase phase, int id, Point p, double time, bool allow_hold = false)
  {
    std::vector<Action> out;
    if (phase == Phase::Cancel) {
      if (drag_ || held_) {
        out.push_back({ActionType::Cancel, origin_, {}});
      }
      cancel();
      return out;
    }
    if (phase == Phase::Down) {
      stop_inertia();
      if (contacts_.contains(id)) {
        return out;
      }
      if (contacts_.empty()) {
        *this = {};
        start_time_ = time;
        origin_ = p;
        primary_ = id;
        allow_hold_ = allow_hold;
      }
      if (draining_) {
        tap_ = false;
      }
      contacts_[id] = {p, p};
      if ((contacts_.size() == 2 || contacts_.size() == 3) && !drag_ && !held_ && !draining_) {
        multi_ = true;
        tap_ = contacts_.size() == 2 && time - start_time_ <= 0.120;
        rebase_geometry(time);
      }
      if (contacts_.size() > 3) {
        draining_ = true;
        tap_ = false;
        velocity_ = {};
      }
      return out;
    }
    auto found = contacts_.find(id);
    if (found == contacts_.end()) {
      return out;
    }
    found->second.position = p;
    if ((p - found->second.start).length() > 12) {
      tap_ = false;
      moved_ = true;
    }
    if (phase == Phase::Motion && !draining_) {
      if (multi_ && (contacts_.size() == 2 || contacts_.size() == 3)) {
        const Point previous_center = center_;
        const double previous_distance = distance_;
        const auto [center, distance] = geometry();
        if (moved_ || navigated_) {
          out.push_back(
              {ActionType::Navigate,
               center,
               previous_center,
               previous_distance > 1 && distance > 1 ? distance / previous_distance : 1});
          navigated_ = true;
        }
        center_ = center;
        distance_ = distance;
        record_center(time);
      }
      else if (!multi_ && id == primary_) {
        if (!held_ && !drag_ && (p - origin_).length() > 12) {
          drag_ = true;
          out.push_back({ActionType::Move, origin_, {}});
          out.push_back({ActionType::Press, origin_, {}});
        }
        if (drag_ || held_) {
          out.push_back({ActionType::Move, p, {}});
        }
      }
    }
    if (phase == Phase::Up) {
      if (multi_ && !draining_ && contacts_.size() == 2) {
        draining_ = true;
        release_time_ = time;
        tick_time_ = time;
        if (navigated_ && history_.size() >= 2 && time - history_.back().first < 0.100) {
          const auto &first = history_.front();
          const auto &last = history_.back();
          if (last.first > first.first) {
            velocity_ = (last.second - first.second) * (1 / (last.first - first.first));
          }
        }
      }
      if (!multi_ && id == primary_) {
        if (!drag_ && !held_) {
          out.push_back({ActionType::Move, p, {}});
          out.push_back({ActionType::Press, p, {}});
        }
        out.push_back({ActionType::Release, p, {}});
      }
      contacts_.erase(found);
      if (multi_ && !draining_ && contacts_.size() == 2) {
        tap_ = false;
        rebase_geometry(time);
      }
      if (contacts_.empty()) {
        if (multi_ && tap_ && time - start_time_ <= 0.300) {
          out.push_back({ActionType::RightClick, center_, {}});
        }
        inertia_ = multi_ && navigated_ && velocity_.length() >= 80 &&
                   time - release_time_ < 0.100;
        release_time_ = tick_time_ = time;
      }
    }
    return out;
  }

  std::vector<Action> tick(double time)
  {
    if (contacts_.size() == 1 && !multi_ && !drag_ && !held_ && !draining_ && allow_hold_ &&
        time - start_time_ >= 0.500)
    {
      held_ = true;
      return {{ActionType::Hold, origin_, {}}};
    }
    if (inertia_) {
      const double dt = std::max(0.0, time - tick_time_);
      tick_time_ = time;
      const double decay = std::exp(-dt / 0.180);
      Point previous = center_;
      center_ = center_ + velocity_ * (0.180 * (1 - decay));
      velocity_ = velocity_ * decay;
      if (velocity_.length() < 10 || time - release_time_ >= 1.5) {
        stop_inertia();
      }
      return {{ActionType::Navigate, center_, previous}};
    }
    return {};
  }
};
}  // namespace blender::wm::touch
