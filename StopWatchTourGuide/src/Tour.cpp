#include "Tour.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace tour {
bool validCourse(const Course& c) {
  if (c.name.empty() || c.name.size() > 96 || c.segments.empty() ||
      c.segments.size() > kMaxSegments) return false;
  for (const auto& s : c.segments)
    if (s.name.empty() || s.name.size() > 96 || s.seconds <= 0) return false;
  return true;
}
bool validRecord(const Record& r) {
  if (r.version != kRecordVersion || !validCourse(r.course) ||
      r.segments.size() != r.course.segments.size() || r.questions.size() > kMaxQuestions ||
      r.started < toSeconds({2000, 1, 1, 0, 0, 0}) ||
      r.deadline <= r.started || r.deadline > toSeconds({2099, 12, 31, 23, 59, 59}) ||
      r.elapsed > static_cast<Millis>((toSeconds({2100,1,1,0,0,0}) - r.started) * 1000)) return false;
  Millis end = 0;
  bool unvisited = false;
  for (size_t i = 0; i < r.segments.size(); ++i) {
    const auto& s = r.segments[i];
    if (!s.visited) {
      if (s.start || s.end || i == 0) return false;
      unvisited = true;
    } else {
      if (unvisited || s.start != end || s.end < s.start || s.end > r.elapsed) return false;
      end = s.end;
    }
  }
  if (end != r.elapsed) return false;
  Millis previous = 0;
  for (const auto& q : r.questions) {
    if (q.segment >= r.segments.size() || q.elapsed < previous) return false;
    const auto& s = r.segments[q.segment];
    if (!s.visited || q.elapsed < s.start || q.elapsed > s.end) return false;
    previous = q.elapsed;
  }
  return true;
}
StartError Tour::start(const Course& c, LocalSeconds now, bool clockSet,
                      LocalSeconds deadline, Millis monotonic, WarningTimes warnings) {
  if (running_) return StartError::Running;
  if (!validCourse(c) || warnings.overall <= 0 || warnings.segment <= 0) return StartError::InvalidCourse;
  if (!clockSet || now < toSeconds({2000, 1, 1, 0, 0, 0}) ||
      now > toSeconds({2099, 12, 31, 23, 59, 59})) return StartError::ClockUnset;
  if (deadline <= now || deadline > toSeconds({2099, 12, 31, 23, 59, 59}))
    return StartError::InvalidDeadline;
  record_ = Record{};
  record_.course = c;
  record_.started = now;
  record_.deadline = deadline;
  record_.segments.resize(c.segments.size());
  record_.questions.reserve(kMaxQuestions);
  warnings_ = warnings;
  origin_ = monotonic;
  running_ = true;
  overallWarned_ = deadlineNotified_ = false;
  enterSegment(0, monotonic);
  return StartError::None;
}
Millis Tour::elapsed(Millis now) const {
  return running_ ? (now >= origin_ ? now - origin_ : 0) : record_.elapsed;
}
int64_t Tour::remainingMs(Millis now) const {
  return (record_.deadline - record_.started) * 1000 - static_cast<int64_t>(elapsed(now));
}
void Tour::enterSegment(size_t index, Millis now) {
  segment_ = index;
  const auto start = elapsed(now);
  record_.segments[index] = {true, start, start};
  segmentWarned_ = false;

  const int64_t availableMs = std::max<int64_t>(0, remainingMs(now));
  const auto& segments = record_.course.segments;
  segmentBudgetMs_ = availableMs;
  if (availableMs == 0 || index + 1 == segments.size()) return;

  int64_t remainingWeight = 0;
  for (size_t i = index; i < segments.size(); ++i) remainingWeight += segments[i].seconds;
  // Round the assigned budget once so the displayed seconds and warning agree.
  const double shareSeconds = (availableMs / 1000.0) * segments[index].seconds / remainingWeight;
  const int64_t seconds = std::max<int64_t>(1, std::llround(shareSeconds));
  segmentBudgetMs_ = std::min(availableMs, seconds * 1000);
}
int64_t Tour::segmentRemainingMs(Millis now) const {
  if (record_.segments.empty()) return 0;
  return segmentBudgetMs_ - static_cast<int64_t>(elapsed(now) - record_.segments[segment_].start);
}
Outcome Tour::update(Millis now, Input input) {
  Outcome out;
  if (!running_) return out;
  const auto e = elapsed(now);
  if (input.bShort) {
    if (record_.questions.size() < kMaxQuestions) {
      record_.questions.push_back({static_cast<uint32_t>(segment_), e});
      out.notice = Notice::Confirm;
    } else out.questionFull = true;
  }
  if (input.aLong) {
    record_.segments[segment_].end = record_.elapsed = e;
    running_ = false;
    out.finished = true;
    out.notice = Notice::None;
    return out;
  }
  if (input.aShort) {
    if (segment_ + 1 < record_.segments.size()) {
      record_.segments[segment_].end = e;
      enterSegment(segment_ + 1, now);
      out.segmentChanged = true;
      out.notice = Notice::Confirm;
    } else out.lastSegmentHint = true;
  }
  const auto left = remainingMs(now);
  const auto overallWarningMs = int64_t(warnings_.overall) * 1000;
  // Consume all crossed thresholds, even if their notification loses priority.
  const int64_t segmentWarningMs = int64_t(warnings_.segment) * 1000;
  if (!segmentWarned_ && segmentBudgetMs_ > segmentWarningMs &&
      segmentRemainingMs(now) <= segmentWarningMs) {
    segmentWarned_ = true;
    if (!overallWarned_ && left > overallWarningMs) out.notice = Notice::SegmentWarning;
  }
  if (!overallWarned_ && left <= overallWarningMs) {
    overallWarned_ = true;
    out.notice = Notice::OverallWarning;
  }
  if (!deadlineNotified_ && left <= 0) {
    deadlineNotified_ = true;
    out.notice = Notice::Deadline;
  }
  return out;
}
Button::Event Button::update(bool pressed, Millis now) {
  Event e;
  if (pressed != raw_) { raw_ = pressed; changed_ = now; }
  if (raw_ != stable_ && now - changed_ >= 25) {
    stable_ = raw_;
    if (stable_) { down_ = changed_; fired_ = false; }
    else {
      // A delayed loop may observe a release after the long threshold.
      if (!fired_) {
        if (longEnabled_ && changed_ - down_ >= 800) e.longPress = true;
        else e.shortPress = true;
      }
    }
  }
  if (stable_ && raw_ && longEnabled_ && !fired_ && now - down_ >= 800) {
    fired_ = true;
    e.longPress = true;
  }
  return e;
}
static Millis patternLength(Notice n) {
  switch (n) {
    case Notice::Confirm: return 80;
    case Notice::SegmentWarning: return 300;
    case Notice::OverallWarning: return 550;
    case Notice::Deadline: return 600;
    default: return 0;
  }
}
void Haptics::request(Notice n, Millis now) {
  output(now);
  if (n == Notice::None) return;
  if (active_ == Notice::None || n > active_) {
    active_ = n;
    began_ = now;
  } else if (n == Notice::Confirm) {
    // Only operation confirmations may follow a running pattern; never stale warnings.
    if (confirmations_ != UINT32_MAX) ++confirmations_;
  }
}
uint8_t Haptics::output(Millis now) {
  if (active_ != Notice::None && now - began_ >= patternLength(active_)) {
    active_ = confirmations_ ? Notice::Confirm : Notice::None;
    if (confirmations_) --confirmations_;
    began_ = now;
  }
  if (active_ == Notice::None) return 0;
  if (active_ == Notice::OverallWarning && now - began_ >= 200 && now - began_ < 350)
    return 0;
  return active_ == Notice::Confirm ? 150 : 220;
}
static int daysInMonth(int y, int m) {
  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  return days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}
bool validDate(const DateTime& d) {
  return d.year >= 2000 && d.year <= 2099 && d.month >= 1 && d.month <= 12 &&
         d.day >= 1 && d.day <= daysInMonth(d.year, d.month) && d.hour >= 0 &&
         d.hour < 24 && d.minute >= 0 && d.minute < 60 && d.second >= 0 && d.second < 60;
}
LocalSeconds toSeconds(const DateTime& d) {
  int64_t days = 0;
  for (int y = 2000; y < d.year; ++y) days += (y % 4 == 0 ? 366 : 365);
  for (int m = 1; m < d.month; ++m) days += daysInMonth(d.year, m);
  return ((days + d.day - 1) * 24 + d.hour) * 3600 + d.minute * 60 + d.second;
}
DateTime fromSeconds(LocalSeconds s) {
  DateTime d;
  s = std::max<int64_t>(0, s);
  d.second = s % 60; s /= 60;
  d.minute = s % 60; s /= 60;
  d.hour = s % 24;
  auto days = s / 24;
  d.year = 2000;
  while (days >= (d.year % 4 == 0 && (d.year % 100 != 0 || d.year % 400 == 0) ? 366 : 365)) {
    days -= (d.year % 4 == 0 && (d.year % 100 != 0 || d.year % 400 == 0) ? 366 : 365);
    ++d.year;
  }
  d.month = 1;
  while (days >= daysInMonth(d.year, d.month)) days -= daysInMonth(d.year, d.month++);
  d.day = static_cast<int>(days) + 1;
  return d;
}
std::string dateText(LocalSeconds s, bool seconds) {
  const auto d = fromSeconds(s);
  char b[40];
  if (seconds) std::snprintf(b, sizeof(b), "%04d/%02d/%02d %02d:%02d:%02d", d.year,d.month,d.day,d.hour,d.minute,d.second);
  else std::snprintf(b, sizeof(b), "%04d/%02d/%02d %02d:%02d", d.year,d.month,d.day,d.hour,d.minute);
  return b;
}
std::string duration(int64_t ms) {
  const uint64_t s = (ms < 0 ? uint64_t(-(ms + 1)) + 1 : uint64_t(ms)) / 1000;
  char b[40];
  std::snprintf(b, sizeof(b), "%02llu:%02llu", static_cast<unsigned long long>(s / 60), static_cast<unsigned long long>(s % 60));
  return b;
}
std::string difference(int64_t ms) {
  return std::string(ms < 0 ? "-" : "+") + duration(ms);
}
}  // namespace tour
