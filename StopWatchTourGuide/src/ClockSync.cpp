#include "ClockSync.h"
#include <string_view>

namespace tour {
std::string ClockSync::receive(char byte, App& app) {
  if (byte != '\n') {
    if (length_ < sizeof(line_)) line_[length_++] = byte;
    return {};
  }
  const auto length = length_;
  length_ = 0;
  if (!length || length == sizeof(line_)) return {};
  std::string_view line(line_, length);
  if (line.back() == '\r') line.remove_suffix(1);
  constexpr std::string_view set = "TOUR TIME ";
  if (line == "TOUR TIME?")
    return app.screen() == Screen::Standby ? "TOUR TIME READY" : "TOUR TIME BUSY";
  if (line.substr(0, set.size()) != set) return {};
  if (app.screen() != Screen::Standby) return "TOUR TIME BUSY";
  line.remove_prefix(set.size());
  // Seconds of local civil time since 2000-01-01. Ten digits cover the RTC's
  // entire century, and bound the integer conversion before the range check.
  if (line.empty() || line.size() > 10) return "TOUR TIME INVALID";
  LocalSeconds requested = 0;
  for (const char c : line) {
    if (c < '0' || c > '9') return "TOUR TIME INVALID";
    requested = requested * 10 + (c - '0');
  }
  LocalSeconds verified;
  if (!app.syncClock(requested, verified)) return "TOUR TIME FAILED";
  return "TOUR TIME OK " + std::to_string(verified);
}
}  // namespace tour
