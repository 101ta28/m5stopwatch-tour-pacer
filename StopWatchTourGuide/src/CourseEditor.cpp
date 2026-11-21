#include "CourseEditor.h"
#include <charconv>
#include <utility>

namespace tour {
namespace {
bool validName(std::string_view name) {
  if (name.empty() || name.size() > 96 || name.find_first_not_of(' ') == name.npos) return false;
  // Reject controls, overlong encodings, surrogates and incomplete UTF-8.
  for (size_t i = 0; i < name.size();) {
    const uint8_t lead = name[i++];
    if (lead < 0x20 || lead == 0x7f) return false;
    if (lead < 0x80) continue;
    if (lead < 0xc2 || lead > 0xf4) return false;
    const size_t tail = lead < 0xe0 ? 1 : lead < 0xf0 ? 2 : 3;
    if (i + tail > name.size()) return false;
    const uint8_t next = name[i];
    if ((lead == 0xe0 && next < 0xa0) || (lead == 0xed && next >= 0xa0) ||
        (lead == 0xf0 && next < 0x90) || (lead == 0xf4 && next >= 0x90)) return false;
    for (size_t j = 0; j < tail; ++j)
      if ((name[i++] & 0xc0) != 0x80) return false;
  }
  return true;
}
bool positiveSeconds(std::string_view text, int32_t& seconds) {
  if (text.empty()) return false;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), seconds);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size() && seconds > 0;
}
}  // namespace
std::string settingsText(const GuideSettings& settings) {
  std::string text = settings.course.name + "\n" + std::to_string(settings.warnings.overall) +
                     "\t" + std::to_string(settings.warnings.segment);
  for (const auto& segment : settings.course.segments)
    text += "\n" + std::to_string(segment.seconds) + "\t" + segment.name;
  return text;
}
bool parseSettings(std::string_view text, GuideSettings& settings) {
  if (text.size() > kMaxCourseBytes) return false;
  size_t end = text.find('\n');
  if (end == text.npos || !validName(text.substr(0, end))) return false;
  GuideSettings parsed;
  parsed.course.name = text.substr(0, end);
  text.remove_prefix(end + 1);
  end = text.find('\n');
  const auto warnings = text.substr(0, end);
  const auto split = warnings.find('\t');
  if (end == text.npos || split == warnings.npos ||
      !positiveSeconds(warnings.substr(0, split), parsed.warnings.overall) ||
      !positiveSeconds(warnings.substr(split + 1), parsed.warnings.segment)) return false;
  while (end != text.npos) {
    text.remove_prefix(end + 1);
    end = text.find('\n');
    const auto line = text.substr(0, end);
    const auto tab = line.find('\t');
    if (tab == line.npos || !validName(line.substr(tab + 1)) ||
        parsed.course.segments.size() == kMaxSegments) return false;
    int32_t seconds = 0;
    if (!positiveSeconds(line.substr(0, tab), seconds)) return false;
    parsed.course.segments.push_back({std::string(line.substr(tab + 1)), seconds});
  }
  settings = std::move(parsed);
  return true;
}
}  // namespace tour
