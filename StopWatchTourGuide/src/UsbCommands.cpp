#include "UsbCommands.h"
#include <charconv>

namespace tour {
namespace {
// Local civil seconds since 2000-01-01, as used by the RTC sync command.
bool readSeconds(std::string_view text, LocalSeconds& value) {
  if (text.empty() || text.size() > 10 || text.front() == '-') return false;
  const auto end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  return result.ec == std::errc{} && result.ptr == end;
}
}  // namespace

std::string UsbCommands::receive(char byte, App& app) {
  if (byte != '\n') {
    if (length_ < sizeof(line_)) line_[length_++] = byte;
    return {};
  }
  const auto length = length_;
  length_ = 0;
  if (!length) return {};
  std::string_view line(line_, length);
  constexpr std::string_view course = "TOUR COURSE ";
  if (length == sizeof(line_)) {
    receivingCourse_ = false;
    return line.substr(0, course.size()) == course ? "TOUR COURSE INVALID" : "";
  }
  if (line.back() == '\r') line.remove_suffix(1);
  if (line.substr(0, course.size()) == course)
    return courseCommand(line.substr(course.size()), app);
  if (length >= 40) return {};  // Preserve the clock protocol's original line limit.
  constexpr std::string_view set = "TOUR TIME ";
  if (line == "TOUR TIME?")
    return app.screen() == Screen::Standby ? "TOUR TIME READY" : "TOUR TIME BUSY";
  if (line.substr(0, set.size()) != set) return {};
  if (app.screen() != Screen::Standby) return "TOUR TIME BUSY";
  line.remove_prefix(set.size());
  LocalSeconds requested;
  if (!readSeconds(line, requested)) return "TOUR TIME INVALID";
  LocalSeconds verified;
  if (!app.syncClock(requested, verified)) return "TOUR TIME FAILED";
  return "TOUR TIME OK " + std::to_string(verified);
}

std::string UsbCommands::courseCommand(std::string_view command, App& app) {
  if (app.screen() != Screen::Standby) {
    receivingCourse_ = false;
    return "TOUR COURSE BUSY";
  }
  if (command == "BEGIN") {
    courseText_.clear();
    receivingCourse_ = true;
    return "TOUR COURSE READY";
  }
  if (!receivingCourse_) return "TOUR COURSE INVALID";
  constexpr std::string_view line = "LINE ";
  if (command.substr(0, line.size()) == line) {
    const auto data = command.substr(line.size());
    const auto separator = courseText_.empty() ? 0u : 1u;
    if (!data.empty() && courseText_.size() + separator + data.size() <= kMaxCourseBytes) {
      if (separator) courseText_ += '\n';
      courseText_.append(data);
      return "TOUR COURSE NEXT";
    }
  }
  receivingCourse_ = false;
  constexpr std::string_view save = "SAVE ";
  if (command.substr(0, save.size()) != save) return "TOUR COURSE INVALID";
  command.remove_prefix(save.size());
  const auto split = command.find(' ');
  if (split == command.npos || command.substr(split + 1) != std::to_string(courseText_.size()))
    return "TOUR COURSE INVALID";
  StartPlan plan;
  auto timing = command.substr(0, split);
  if (!timing.empty() && timing.front() == '+') {
    plan.mode = TimingMode::Duration;
    if (!readSeconds(timing.substr(1), plan.seconds)) return "TOUR COURSE DURATION";
  } else if (timing != "-") {
    plan.mode = TimingMode::Deadline;
    if (!readSeconds(timing, plan.seconds)) return "TOUR COURSE DEADLINE";
  }
  switch (app.saveCourse(courseText_, plan)) {
    case CourseSave::Saved: return "TOUR COURSE OK";
    case CourseSave::Invalid: return "TOUR COURSE INVALID";
    case CourseSave::ClockUnset: return "TOUR COURSE CLOCK";
    case CourseSave::InvalidDeadline: return "TOUR COURSE DEADLINE";
    case CourseSave::InvalidDuration: return "TOUR COURSE DURATION";
    default: return "TOUR COURSE FAILED";
  }
}
}  // namespace tour
