#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tour {
using Millis = uint64_t;
using LocalSeconds = int64_t;  // Local civil time, deliberately independent of TZ/DST.
constexpr uint32_t kRecordVersion = 1;
constexpr uint32_t kDurationRecordVersion = 2;
constexpr size_t kMaxSegments = 32;
constexpr size_t kMaxQuestions = 1024;
struct Segment { std::string name; int32_t seconds; };
struct Course { std::string name; std::vector<Segment> segments; };
struct SegmentResult { bool visited = false; Millis start = 0, end = 0; };
struct Question { uint32_t segment; Millis elapsed; };
struct Record {
  uint32_t version = kRecordVersion;
  Course course;
  LocalSeconds deadline = 0, started = 0;
  int32_t durationSeconds = 0;  // Duration runs record elapsed time without calendar timestamps.
  Millis elapsed = 0;
  std::vector<SegmentResult> segments;
  std::vector<Question> questions;
  LocalSeconds ended() const { return started + static_cast<int64_t>(elapsed / 1000); }
  int64_t plannedSeconds() const { return durationSeconds ? durationSeconds : deadline - started; }
  int64_t deadlineDifferenceMs() const {
    return static_cast<int64_t>(elapsed) - plannedSeconds() * 1000;
  }
};
bool validCourse(const Course& course);
bool validRecord(const Record& record);
struct WarningTimes { int32_t overall = 120, segment = 30; };  // Seconds before the boundary.
enum class Notice : uint8_t { None, Confirm, SegmentWarning, SegmentExpired, OverallWarning, Deadline };
enum class StartError { None, Running, InvalidCourse, ClockUnset, InvalidDeadline };
struct Input { bool aShort = false, aLong = false, bShort = false; };
struct Outcome {
  Notice notice = Notice::None;
  bool finished = false, segmentChanged = false, lastSegmentHint = false, questionFull = false;
};
class Tour {
 public:
  StartError start(const Course&, LocalSeconds now, bool clockSet,
                   LocalSeconds deadline, Millis monotonic, WarningTimes warnings = {});
  StartError startFor(const Course&, int32_t seconds, Millis monotonic, WarningTimes warnings = {});
  // Inputs are processed before notices. B is applied before A.
  Outcome update(Millis now, Input input = {});
  bool running() const { return running_; }
  const Record& record() const { return record_; }
  size_t segment() const { return segment_; }
  Millis elapsed(Millis now) const;
  int64_t remainingMs(Millis now) const;
  int64_t segmentBudgetMs() const { return segmentBudgetMs_; }
  int64_t segmentRemainingMs(Millis now) const;
 private:
  StartError beginRun(const Course&, Record timing, Millis monotonic, WarningTimes);
  void enterSegment(size_t index, Millis now);
  Record record_;
  WarningTimes warnings_;
  Millis origin_ = 0;
  size_t segment_ = 0;
  int64_t segmentBudgetMs_ = 0;
  bool running_ = false, overallWarned_ = false, deadlineNotified_ = false;
  bool segmentWarned_ = false, segmentExpiryNotified_ = false;
};

// Called with the current button level, not edge events. No delay or busy wait.
class Button {
 public:
  explicit Button(bool longPress = true) : longEnabled_(longPress) {}
  struct Event { bool shortPress = false, longPress = false; };
  Event update(bool pressed, Millis now);
 private:
  bool raw_ = false, stable_ = false, fired_ = false, longEnabled_;
  Millis changed_ = 0, down_ = 0;
};

class Haptics {
 public:
  void request(Notice notice, Millis now);
  uint8_t output(Millis now);  // Drive the motor only when this value changes.
  void stop() { active_ = Notice::None; confirmations_ = 0; }
  void cancelSegmentNotices() {
    if (active_ == Notice::SegmentWarning || active_ == Notice::SegmentExpired) active_ = Notice::None;
  }
 private:
  Notice active_ = Notice::None;
  uint32_t confirmations_ = 0;
  Millis began_ = 0;
};

struct DateTime { int year = 2026, month = 1, day = 1, hour = 0, minute = 0, second = 0; };
bool validDate(const DateTime&);
LocalSeconds toSeconds(const DateTime&);
DateTime fromSeconds(LocalSeconds);
std::string dateText(LocalSeconds, bool seconds = false);
std::string duration(int64_t milliseconds); // Absolute mm:ss, unlimited minutes.
std::string difference(int64_t milliseconds);
}  // namespace tour
