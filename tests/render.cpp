// Render the same RGB565 sprite as the device, without opening a window or USB port.
#include "../StopWatchTourGuide/src/WatchUi.h"
#include <lgfx/v1/LGFX_Sprite.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace tour;
void checkTouchCoordinates() {
  struct SampleTouch : lgfx::ITouch {
    lgfx::touch_point_t point;
    bool init() override { return true; }
    void wakeup() override {}
    void sleep() override {}
    uint_fast8_t getTouchRaw(lgfx::touch_point_t* points, uint_fast8_t count) override {
      if (!count) return 0;
      points[0] = point;
      return 1;
    }
  } touch;
  for (const int range : {233, 466}) { // CST820 and CST9217 board descriptors
    auto limits = touch.config();
    limits.x_max = limits.y_max = range;
    touch.config(limits);
    lgfx::Panel_NULL panel;
    // Match the AMOLED framebuffer constructor: touch is attached before config.
    panel.setTouch(&touch);
    auto config = panel.config();
    config.panel_width = config.panel_height = 468;
    panel.config(config);
    configureWatchTouch(panel);
    for (lgfx::touch_point_t sample : {lgfx::touch_point_t{233,49}, {148,369}, {311,396}, {151,403}}) {
      touch.point = sample;
      lgfx::touch_point_t result;
      if (panel.getTouch(&result, 1) != 1 || result.x != sample.x || result.y != sample.y)
        throw std::runtime_error("Touch coordinates shifted (controller range " + std::to_string(range) + ")");
    }
  }
}

View readView(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("Missing snapshot: " + path);
  View view;
  std::string line;
  while (std::getline(in,line)) {
    std::istringstream fields(line);
    std::string kind;
    int ink;
    fields >> kind;
    if (kind == "box") {
      Box box{};
      fields >> box.x >> box.y >> box.width >> box.height >> ink >> box.radius;
      box.ink = static_cast<Ink>(ink);
      view.boxes.push_back(box);
    } else if (kind == "ring") {
      fields >> view.remainingPercent >> ink;
      view.ringInk = static_cast<Ink>(ink);
    } else if (kind == "segments") {
      fields >> view.segmentIndex >> view.segmentCount;
      if (!fields.eof()) fields >> view.segmentRemainingFraction;
    } else {
      TextRow row;
      int bg;
      fields >> row.x >> row.y >> row.size >> row.width >> ink >> bg;
      fields.get();
      std::getline(fields,row.text);
      row.ink = static_cast<Ink>(ink);
      row.background = static_cast<Ink>(bg);
      view.rows.push_back(row);
    }
    if (fields.fail()) throw std::runtime_error("Invalid snapshot row: " + line);
  }
  return view;
}

void checkLabels(lgfx::LGFX_Sprite& canvas, const View& view) {
  std::vector<bool> occupied(466 * 466);
  for (auto row : view.rows) {
    row.ink = Ink::Normal;
    row.background = Ink::Black;
    View isolated;
    isolated.rows.push_back(row);
    drawWatchView(canvas, isolated);
    const auto* pixels = static_cast<const uint16_t*>(canvas.getBuffer());
    unsigned lit = 0;
    bool antialiased = false;
    uint16_t firstShade = 0;
    for (int y = 0; y < 466; ++y) for (int x = 0; x < 466; ++x) {
      const int i = y * 466 + x;
      if (!pixels[i]) continue;
      ++lit;
      if (!firstShade) firstShade = pixels[i];
      else if (pixels[i] != firstShade) antialiased = true;
      if (occupied[i] || (x-233)*(x-233)+(y-233)*(y-233) > 226*226 ||
          2*std::abs(x-row.x) > row.width+2)
        throw std::runtime_error("Overlapping or clipped label: " + row.text);
      occupied[i] = true;
    }
    if ((row.size == 76 || row.text == "施設案内") && !antialiased)
      throw std::runtime_error("Glyph coverage has no intermediate shades: " + row.text);
    if (!lit) throw std::runtime_error("Missing text: " + row.text);
  }
}

void checkSegmentCountdown(lgfx::LGFX_Sprite& canvas) {
  View view;
  view.remainingPercent = 50;
  view.segmentCount = 2; // The first segment occupies the right semicircle.
  drawWatchView(canvas, view);
  const auto full = canvas.readPixel(379, 87);
  const auto inactive = canvas.readPixel(87, 87);
  if (full == inactive || full != canvas.readPixel(379, 379))
    throw std::runtime_error("New segment is not full");
  view.segmentRemainingFraction = 0;
  drawWatchView(canvas, view);
  const auto empty = canvas.readPixel(379, 87);
  if (empty == full || empty == inactive || empty != canvas.readPixel(379, 379))
    throw std::runtime_error("Expired segment lost its position or still shows time");
  view.segmentRemainingFraction = 0.5f;
  drawWatchView(canvas, view);
  if (canvas.readPixel(379, 87) != full || canvas.readPixel(379, 379) != empty)
    throw std::runtime_error("Half of the current segment should be filled");
}

void checkRingClearance(lgfx::LGFX_Sprite& canvas, View rings) {
  if (rings.segmentCount < 2) return;
  const auto* pixels = static_cast<const uint16_t*>(canvas.getBuffer());
  const std::vector<uint16_t> screen(pixels, pixels + canvas.width() * canvas.height());
  rings.rows.clear();
  rings.boxes.clear();
  drawWatchView(canvas, rings);
  for (size_t i = 0; i < screen.size(); ++i)
    if (pixels[i] && pixels[i] != screen[i])
      throw std::runtime_error("Running screen obscures a ring");
}

int main(int argc, char** argv) {
  checkTouchCoordinates();
  lgfx::LGFX_Sprite canvas;
  canvas.setColorDepth(16);
  if (!canvas.createSprite(466,466)) return 1;
  checkSegmentCountdown(canvas);
  for (int i = 1; i < argc; ++i) {
    const std::string stem = argv[i];
    const View view = readView(stem+".tsv");
    checkLabels(canvas, view);
    drawWatchView(canvas, view);
    std::ofstream image(stem+".ppm", std::ios::binary);
    image << "P6\n466 466\n255\n";
    std::vector<uint8_t> rgb(466*466*3);
    canvas.readRectRGB(0,0,466,466,rgb.data());
    image.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    if (!image) return 1;
    checkRingClearance(canvas, view);
  }
}
