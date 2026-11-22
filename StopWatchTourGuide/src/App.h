#pragma once
#include "CourseEditor.h"

namespace tour {
class Clock {
 public:
  virtual ~Clock() = default;
  virtual bool read(LocalSeconds& now) = 0;
  virtual bool set(const DateTime&) = 0;
};
enum class Screen { Standby, Running, Review, EditDeadline, EditClock, CoursePlan, WifiEdit };
enum class Ink { Normal, Muted, Accent, Warning, Surface, Primary, OnPrimary, Track, Black, Segment, SegmentTrack };
enum class Action { None, Clock, Deadline, Start, Course, Records, Back, Save, Retry,
                    PreviousValue, NextValue, Year, Month, Day, Hour, Minute,
                    NextSegment, Question, NextPage, WifiEdit };
struct TextRow {
  std::string text;
  int x, y, size, width;
  Ink ink = Ink::Normal;
  Ink background = Ink::Black;
};
struct Box { int x, y, width, height, radius; Ink ink; Action action; };
struct View {
  std::vector<TextRow> rows;
  std::vector<Box> boxes;
  int remainingPercent = -1;
  Ink ringInk = Ink::Accent;
  size_t segmentIndex = 0;
  size_t segmentCount = 0;
  float segmentRemainingFraction = 1.0f;
};
// Coordinates are for the StopWatch's 466 x 466 round panel.
class App {
 public:
  App(Course course, Clock& clock, RecordStore& store, CourseEditor& editor);
  void begin();
  bool syncClock(LocalSeconds requested, LocalSeconds& verified);
  void update(Millis now, Input input = {});
  void tap(int x, int y, Millis now);
  void releaseTouch(int startX, int startY, int endX, int endY, Millis now);
  void swipe(int dy);
  View view(Millis now);
  bool needsTimeRefresh(Millis lastFrame, Millis now) const;
  uint8_t motor(Millis now) { return haptics_.output(now); }
  Screen screen() const { return screen_; }
  const Tour& tour() const { return tour_; }
  bool unsaved() const { return unsaved_; }
  bool deadlineConfirmed() const { return deadlineConfirmed_; }
 private:
  GuideSettings settings_;
  Clock& clock_;
  RecordStore& store_;
  CourseEditor& editor_;
  WifiAccess access_;
  Tour tour_;
  Haptics haptics_;
  Screen screen_ = Screen::Standby;
  Record review_;
  bool haveReview_ = false, unsaved_ = false, deadlineConfirmed_ = false;
  LocalSeconds deadline_ = 0;
  DateTime edit_;
  int field_ = 0;
  size_t reviewPage_ = 0, questionOffset_ = 0, courseOffset_ = 0;
  std::string message_;
  Millis messageUntil_ = 0, deadlineBannerUntil_ = 0;
  void message(const std::string&, Millis now);
  void openEditor(bool clock, Millis now);
  void changeField(int direction);
  void confirmDate(Millis now);
  void saveResult(Millis now);
};
}  // namespace tour
