#include "WatchUi.h"
#include <lgfx/v1/LGFXBase.hpp>
#include <algorithm>
#include <cmath>

namespace tour {
namespace {
#include "fonts/GlyphData.h"

struct Face {
  int size;
  lgfx::PointerWrapper data;
  lgfx::VLWfont font;
  bool ready;
  template<size_t N> Face(int size, const uint8_t (&bytes)[N])
      : size(size), data(bytes, N), ready(font.loadFont(&data)) {}
};

uint32_t color(Ink ink) {
  switch (ink) {
    case Ink::Normal: return 0xf5f5f7;
    case Ink::Muted: return 0xb7bbc3;
    case Ink::Accent: return 0x68caff;
    case Ink::Segment: return 0xa6d980;
    case Ink::SegmentTrack: return 0x526c44;
    case Ink::Warning: return 0xffb04e;
    case Ink::Surface: return 0x24262b;
    case Ink::Primary: return 0x77ceff;
    case Ink::OnPrimary: return 0x061923;
    case Ink::Track: return 0x303238;
    default: return 0;
  }
}

void drawSegmentRing(lgfx::LGFXBase& canvas, const View& view) {
  if (view.segmentCount < 2) return;
  constexpr int radius = 207;
  constexpr int gapRadius = 5;
  constexpr float radiansPerDegree = 3.14159265f / 180.0f;
  const float step = 360.0f / view.segmentCount;
  canvas.fillArc(233, 233, radius+1, radius-1, 0, 360, color(Ink::Track));
  for (size_t i = 0; i < view.segmentCount; ++i) {
    const float angle = i * step * radiansPerDegree;
    const int x = 233 + std::lround(radius * std::sin(angle));
    const int y = 233 - std::lround(radius * std::cos(angle));
    canvas.fillCircle(x, y, gapRadius, color(Ink::Black));
  }
  // Map the remaining time to the visible arc between the two gaps.
  constexpr float gapAngle = gapRadius / (radius * radiansPerDegree);
  const float start = -90.0f + view.segmentIndex * step + gapAngle;
  const float span = step - 2 * gapAngle;
  const float end = start + span;
  canvas.fillArc(233, 233, radius+2, radius-3, start, end, color(Ink::SegmentTrack));
  if (view.segmentRemainingFraction > 0) {
    canvas.fillArc(233, 233, radius+2, radius-3,
                   start + span * (1.0f - view.segmentRemainingFraction), end, color(Ink::Segment));
  }
}

bool covers(const lgfx::VLWfont& font, const std::string& text) {
  uint32_t code = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const auto b = static_cast<uint8_t>(text[i]);
    if (b < 0x80) code = b;
    else if (b < 0xc0) code = (code << 6) | (b & 0x3f);
    else code = b & (b < 0xe0 ? 0x1f : b < 0xf0 ? 0x0f : 0x07);
    if (i + 1 < text.size() && (text[i + 1] & 0xc0) == 0x80) continue;
    uint16_t index;
    if (code > 0xffff || !font.getUnicodeIndex(code, &index)) return false;
  }
  return true;
}

void drawRow(lgfx::LGFXBase& canvas, const TextRow& row) {
  static Face faces[] = {{76,pixels76}, {56,pixels56}, {32,pixels32}, {24,pixels24}, {20,pixels20}};
  const int dy = std::abs(row.y - 233) + row.size / 2 + 3;
  const int chord = std::sqrt(std::max(0, 225 * 225 - dy * dy));
  const int limit = std::max(1, std::min(row.width, 2 * (chord - std::abs(row.x - 233))));
  // Prefer a smaller native strike over scaling a glyph. The general Japanese
  // font also covers user-edited course names and names in earlier records.
  canvas.setTextSize(1);
  bool found = false;
  for (auto& face : faces) {
    if (face.size > row.size || !face.ready || !covers(face.font, row.text)) continue;
    canvas.setFont(&face.font);
    found = true;
    if (canvas.textWidth(row.text.c_str()) <= limit) break;
  }
  if (!found) canvas.setFont(&lgfx::fonts::lgfxJapanGothic_24);
  const int width = canvas.textWidth(row.text.c_str());
  if (width > limit) canvas.setTextSize(float(limit) / width);
  canvas.setTextColor(color(row.ink), color(row.background));
  canvas.drawString(row.text.c_str(), row.x, row.y);
}
}  // namespace

void configureWatchTouch(lgfx::Panel_Device& panel) {
  // The factory C152 uses touch coordinates directly as portrait pixels.
  // M5GFX 0.3.0 calibrated the AMOLED framebuffer before setting its size.
  float nativePixels[6] = {1, 0, 0, 0, 1, 0};
  panel.setCalibrateAffine(nativePixels);
}

void drawWatchView(lgfx::LGFXBase& canvas, const View& view) {
  canvas.fillScreen(color(Ink::Black));
  if (view.remainingPercent >= 0) {
    constexpr int radius = 220;
    constexpr float end = 270.0f; // Twelve o'clock.
    canvas.fillArc(233, 233, radius, radius-5, 0, 360, color(Ink::Track));
    if (view.remainingPercent > 0)
      canvas.fillArc(233, 233, radius, radius-5, end - view.remainingPercent * 3.6f,
                     end, color(view.ringInk));
    drawSegmentRing(canvas, view);
  }
  for (const auto& box : view.boxes)
    canvas.fillSmoothRoundRect(box.x, box.y, box.width, box.height, box.radius, color(box.ink));
  canvas.setTextDatum(lgfx::textdatum_t::middle_center);
  for (const auto& row : view.rows) drawRow(canvas, row);
}
}  // namespace tour
