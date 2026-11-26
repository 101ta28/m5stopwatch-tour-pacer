#pragma once
#include "App.h"
#include <optional>

namespace tour {
class DisplayState {
 public:
  static constexpr uint8_t normalBrightness = 160;

  void toggle(Millis now) {
    off_ = !off_;
    touchScreen_.reset();
    noteActivity(now);
  }
  bool off() const { return off_; }
  void noteActivity(Millis now) { lastActivity_ = now; }
  uint8_t brightness(Millis now) const {
    if (off_) return 0;
    return now - lastActivity_ >= 30000 ? 35 : normalBrightness;
  }

  void beginTouch(Screen screen) {
    if (!off_) touchScreen_ = screen;
  }
  bool endTouch(Screen screen) {
    const bool accepted = touchScreen_ == screen;
    touchScreen_.reset();
    return accepted;
  }

 private:
  bool off_ = false;
  Millis lastActivity_ = 0;
  std::optional<Screen> touchScreen_;
};
}  // namespace tour
