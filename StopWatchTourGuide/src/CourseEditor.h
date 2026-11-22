#pragma once
#include "RecordStore.h"
#include <string_view>

namespace tour {
struct GuideSettings { Course course; WarningTimes warnings; };
struct WifiAccess { std::string ssid, password, url; };
enum class CourseSave { Saved, Invalid, Failed, ClockUnset, InvalidDeadline, InvalidDuration };

class CourseEditor {
 public:
  virtual ~CourseEditor() = default;
  virtual LoadResult load(GuideSettings& settings) = 0;
  CourseSave save(std::string_view text, GuideSettings& settings);
  virtual bool open(const GuideSettings& settings, WifiAccess& access) = 0;
  // Stop serving requests before returning the last successfully saved settings.
  virtual void close(GuideSettings& settings) = 0;
 protected:
  virtual bool writeCourse(std::string_view text) = 0;
};

// UTF-8 lines: course name, warning seconds (overall<TAB>segment), then one "seconds<TAB>name" line per segment.
// Both the HTTP body and the versioned NVS key use this bounded representation.
constexpr size_t kMaxCourseBytes = 4096;
std::string settingsText(const GuideSettings& settings);
bool parseSettings(std::string_view text, GuideSettings& settings);
}  // namespace tour
