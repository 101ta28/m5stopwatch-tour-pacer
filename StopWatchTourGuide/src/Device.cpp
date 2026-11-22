#include "Device.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <utility/M5IOE1_Class.hpp>
#include <lgfx/v1/panel/Panel_CO5300.hpp>
#include <esp_timer.h>
#include "NvsBlob.h"
#include "WifiCourseEditor.h"
#include <algorithm>
#include "../CourseConfig.h"
#include "App.h"
#include "UsbCommands.h"
#include "WatchUi.h"

namespace {
using namespace tour;
class NvsBlobs final : public BlobStore {
 public:
  ReadResult read(unsigned slot, std::vector<uint8_t>& data) override {
    return readNvsBlob(slot == 0 ? "record0" : "record1", kMaxRecordBytes, data);
  }
  bool write(unsigned slot, const std::vector<uint8_t>& data) override {
    return writeNvsBlob(slot == 0 ? "record0" : "record1", data.data(), data.size());
  }
};
class RtcClock final : public Clock {
 public:
  bool read(LocalSeconds& now) override {
    if (!M5.Rtc.isEnabled()) return false;
    auto* chip = M5.Rtc.getRtcInstancePtr();
    uint8_t flags = 0;
    // RX8130's VLF (bit1) records oscillation stop / possibly invalid time.
    // M5Unified 0.2.28 getVoltLow() checks VBLF (bit7), a different flag.
    if (!chip || !chip->readRegister(0x1D, &flags, 1) || (flags & 0x02)) return false;
    m5::rtc_datetime_t rtc;
    if (!M5.Rtc.getDateTime(&rtc)) return false;
    DateTime d{rtc.date.year, rtc.date.month, rtc.date.date,
               rtc.time.hours, rtc.time.minutes, rtc.time.seconds};
    if (!validDate(d)) return false;
    now = toSeconds(d);
    return true;
  }
  bool set(const DateTime& d) override {
    if (!validDate(d) || !M5.Rtc.isEnabled()) return false;
    m5::rtc_datetime_t rtc;
    rtc.date.year = d.year;
    rtc.date.month = d.month;
    rtc.date.date = d.day;
    // 2000-01-01 was Saturday; RTC expects Sunday=0.
    rtc.date.weekDay = (toSeconds(d) / 86400 + 6) % 7;
    rtc.time.hours = d.hour;
    rtc.time.minutes = d.minute;
    rtc.time.seconds = d.second;
    auto* chip = M5.Rtc.getRtcInstancePtr();
    if (!chip || !chip->setDateTime(&rtc.date, &rtc.time)) return false;
    // A verified user-set time acknowledges VLF. Reg 0x1D is W0C:
    // clear bit1, preserve other flags, keep reserved bit6 at zero.
    // See Epson RX8130CE application manual, sections 13.2 and 14.5.
    m5::rtc_datetime_t check;
    if (!M5.Rtc.getDateTime(&check)) return false;
    DateTime readBack{check.date.year, check.date.month, check.date.date,
                     check.time.hours, check.time.minutes, check.time.seconds};
    if (!validDate(readBack)) return false;
    const auto delta = toSeconds(readBack) - toSeconds(d);
    if (delta < 0 || delta > 1 || !chip->writeRegister8(0x1D, 0xBD)) return false;
    LocalSeconds verified;
    return read(verified);
  }
};
NvsBlobs blobs;
RecordStore records(blobs);
RtcClock rtc;
WifiCourseEditor editor;
App app(configuredCourse(), rtc, records, editor);
UsbCommands usbCommands;
Button buttonA;
Button buttonB(false);
M5Canvas canvas(&M5.Display);
bool canvasReady = false;
const char* bootStage = "setup";
bool scanBeforeWake = false, scanAfterWake = false;
int panelPowerLevel = -1, panelResetLevel = -1;
int panelMode = -1, panelOutput = -1, panelRevision = -1, pmicPower = -1;
bool rtcPowerHold = false;
int rtcBootFlags = -1;
const char* panelStartup = "not checked";
uint16_t firstCanvasPixel = 0, firstDisplayPixel = 0;
uint8_t motorLevel = 0, brightness = 160;
Millis lastFrame = 0, lastActivity = 0;
Screen touchScreen = Screen::Standby;
Millis monotonic() { return static_cast<Millis>(esp_timer_get_time() / 1000); }
void logStage(const char* stage) {
  bootStage = stage;
  Serial.printf("[tour] %s\n", stage);
}
void logDisplayStatus() {
  Serial.printf("[tour] stage=%s board=%u displays=%u canvas=%u uptime_ms=%lu "
                "panel=%s power=%d reset=%d TE=%u/%u firstPixel=%04x/%04x "
                "ioeRev=%d mode=%d out=%d pmicPower=%d rtcPowerHold=%u rtcBootFlags=%d\n",
                bootStage, unsigned(M5.getBoard()), unsigned(M5.getDisplayCount()),
                unsigned(canvasReady), millis(), panelStartup, panelPowerLevel, panelResetLevel,
                unsigned(scanBeforeWake), unsigned(scanAfterWake),
                unsigned(firstCanvasPixel), unsigned(firstDisplayPixel),
                panelRevision, panelMode, panelOutput, pmicPower, unsigned(rtcPowerHold), rtcBootFlags);
}
volatile bool scanEdge = false;
void ARDUINO_ISR_ATTR onScanEdge() { scanEdge = true; }

int readRegister(const m5::I2C_Device& device, uint8_t reg) {
  uint8_t value;
  return device.readRegister(reg, &value, 1) ? value : -1;
}

bool retainRtcPower(m5::M5PM1_Class& pmic) {
  // C152 L1 supplies the RTC and IMU. As in the factory firmware, latch the
  // enabled LDO for power-button shutdown. The PM1 resets HOLD_CFG on boot.
  constexpr uint8_t ldoEnable = 1u << 2;  // PWR_CFG (0x06)
  constexpr uint8_t ldoHold = 1u << 5;    // HOLD_CFG (0x07)
  for (int attempt = 0; attempt < 3; ++attempt) {
    uint8_t power = 0, hold = 0;
    if (pmic.setLDOOutput(true) && pmic.bitOn(0x07, ldoHold)
        && pmic.readRegister(0x06, &power, 1) && (power & ldoEnable)
        && pmic.readRegister(0x07, &hold, 1) && (hold & ldoHold)) return true;
    delay(2);
  }
  return false;
}

constexpr uint8_t powerBit = 1u << m5::M5IOE1_Class::gpio8;
constexpr uint8_t resetBit = 1u << m5::M5IOE1_Class::gpio5;

// The factory IOE1 driver transfers both bytes of each GPIO register, then
// reads the output latch back. Preserve all pins except the two panel pins.
bool setPanelBits(m5::IOExpander_Base& ioe, uint8_t reg, uint8_t mask, uint8_t bits) {
  uint8_t pair[2];
  if (!ioe.readRegister(reg, pair, sizeof(pair))) return false;
  delay(1);
  pair[0] = (pair[0] & ~mask) | bits;
  if (!ioe.writeRegister(reg, pair, sizeof(pair))) return false;
  delay(1);
  return ioe.readRegister(reg, pair, sizeof(pair)) && (pair[0] & mask) == bits;
}

void samplePanelPins(m5::IOExpander_Base& ioe) {
  uint8_t inputs[2];
  const int pins = ioe.readRegister(0x07, inputs, sizeof(inputs)) ? inputs[0] : -1;
  panelPowerLevel = pins < 0 ? -1 : !!(pins & powerBit);
  panelResetLevel = pins < 0 ? -1 : !!(pins & resetBit);
}

void recoverStopWatchPanel(m5::M5IOE1_Class& ioe) {
  // L3B_EN is also PWM2. Keep the rail enabled while pulsing only OLED_RST.
  constexpr uint8_t pins = powerBit | resetBit;
  for (unsigned attempt = 0; attempt < 5; ++attempt) {
    const bool pwmOff = ioe.setPwmDuty12bit(m5::M5IOE1_Class::pwm_ch2, 0,
                                            m5::pwm_polarity_t::normal, false);
    delay(1);
    const bool configured = pwmOff && setPanelBits(ioe, 0x13, pins, 0) // push-pull
                         && setPanelBits(ioe, 0x03, pins, pins) // output mode
                         && setPanelBits(ioe, 0x05, pins, powerBit);
    // The factory demo waits 80 ms and reads L3B_EN back before display init.
    delay(80);
    samplePanelPins(ioe);
    if (configured && panelPowerLevel == 1 && panelResetLevel == 0) break;
    if (attempt == 4) {
      setPanelBits(ioe, 0x05, pins, pins); // leave reset released on failure
      panelStartup = "power/reset-low-unconfirmed";
      return;
    }
  }
  if (!setPanelBits(ioe, 0x05, pins, pins)) {
    panelStartup = "reset-write-failed";
    return;
  }
  // CO5300 datasheet section 6.6, RESET release to Sleep Out.
  delay(125);
  samplePanelPins(ioe);
  if (panelPowerLevel != 1 || panelResetLevel != 1) {
    panelStartup = "reset-release-unconfirmed";
    return;
  }

  // Reset loses all controller settings. Replay the StopWatch preset on the
  // physical QSPI bus; M5.Display itself points at the PSRAM framebuffer.
  lgfx::Panel_CO5300 controller;
  auto* display = M5.Display.getPanel();
  controller.config(display->config());
  controller.setBus(display->getBus());
  static constexpr uint8_t init[] = {
    0x11, 0x80, 150,    // Sleep Out
    0xc4, 1, 0x80,      // StopWatch panel setting
    0x35, 1, 0x80,      // TE output on
    0x44, 2, 0x01, 0xd2, // TE scan line 466
    0x53, 1, 0x20,      // brightness control on
    0x20, 0,            // normal polarity
    0x36, 1, 0,         // rotation 0
    0x3a, 1, 0x55,      // RGB565
    0x51, 1, 0xa0,      // brightness
    0x29, 0,            // Display On
    0xff, 0xff
  };
  controller.command_list(init);
  panelStartup = "reinitialized";
}

void wakeStopWatchPanel() {
  auto& ioe = static_cast<m5::M5IOE1_Class&>(M5.getIOExpander(0));
  panelRevision = readRegister(ioe, 0x02);
  pmicPower = readRegister(M5.Power.M5pm1, 0x06);
  // G38 is the CO5300's TE (scan timing) output, configured by M5GFX.
  attachInterrupt(38, onScanEdge, RISING);
  scanEdge = false;
  delay(80);
  scanBeforeWake = scanEdge;
  if (scanBeforeWake) panelStartup = "already-scanning";
  else recoverStopWatchPanel(ioe);
  scanEdge = false;
  delay(50);
  scanAfterWake = scanEdge;
  detachInterrupt(38);
  samplePanelPins(ioe);
  panelMode = readRegister(ioe, 0x03);
  panelOutput = readRegister(ioe, 0x05);
}
void setMotor(uint8_t level) {
  if (level == motorLevel) return;
  M5.Power.setVibration(level);
  motorLevel = level;
}
void render(Millis now) {
  if (!canvasReady) return;
  drawWatchView(canvas, app.view(now));
  canvas.pushSprite(0, 0);
}
}  // namespace
void deviceSetup() {
  Serial.begin(115200);
  logStage("initializing StopWatch");
  // This firmware targets the C152. Use its panel and power-up sequence.
  m5gfx::detect_config_t displayConfig;
  displayConfig.fixed_board = m5gfx::board_t::board_M5StopWatch;
  M5.Display.setDetectConfig(displayConfig);
  auto cfg = M5.config();
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  M5.begin(cfg);
  rtcPowerHold = retainRtcPower(M5.Power.M5pm1);
  if (M5.getDisplayCount() == 0) {
    logStage("display init failed");
    return;
  }
  Serial.printf("[tour] display=%dx%d psram=%u\n",
                int(M5.Display.width()), int(M5.Display.height()), unsigned(ESP.getPsramSize()));
  M5.Power.setVibration(0);
  M5.Display.setRotation(0);
  configureWatchTouch(*M5.Display.getPanel());
  wakeStopWatchPanel();
  M5.Display.setBrightness(brightness);
  // Debouncing and 0.8-second semantics are provided by the portable Button.
  M5.BtnA.setDebounceThresh(0);
  M5.BtnB.setDebounceThresh(0);
  canvas.setColorDepth(16);
  canvasReady = canvas.createSprite(466, 466) != nullptr;
  if (!canvasReady) {
    logStage("canvas allocation failed");
    M5.Display.setFont(&fonts::efontJA_24);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("画面メモリ不足",233,233);
    return;
  }
  logStage("loading records and RTC");
  // A cold boot may have probed the RTC before its L1 supply was ready.
  if (!M5.Rtc.isEnabled() && M5.Rtc.begin(&M5.In_I2C, M5.getBoard())) {
    if (cfg.disable_rtc_irq) M5.Rtc.disableIRQ();
  }
  if (auto* chip = M5.Rtc.getRtcInstancePtr()) rtcBootFlags = readRegister(*chip, 0x1D);
  app.begin();
  lastActivity = monotonic();
  lastFrame = lastActivity;
  logStage("drawing first frame");
  render(lastActivity);
  // Sample the filled clock control away from its text.
  firstCanvasPixel = canvas.readPixel(108, 112);
  firstDisplayPixel = M5.Display.readPixel(108, 112);
  logStage("first frame submitted");
}
void deviceLoop() {
  // USB may enumerate after setup. Report the last stage to late monitors.
  static Millis lastStatus = 0;
  const auto now = monotonic();
  if (now - lastStatus >= 3000) {
    lastStatus = now;
    if (Serial) logDisplayStatus();
  }
  if (!canvasReady) { delay(1); return; }
  // A partial command must leave time for button, timer and motor updates.
  for (int i = 0; i < 64 && Serial.available(); ++i) {
    const int byte = Serial.read();
    if (byte < 0) break;
    const auto reply = usbCommands.receive(static_cast<char>(byte), app);
    if (!reply.empty()) Serial.println(reply.c_str());
  }
  M5.update();
  const auto a = buttonA.update(M5.BtnA.isPressed(),now);
  const auto b = buttonB.update(M5.BtnB.isPressed(),now);
  const bool action = a.shortPress || a.longPress || b.shortPress;
  if (action || M5.BtnA.isPressed() || M5.BtnB.isPressed()) lastActivity = now;
  // Storage may take time; switch the motor off before the synchronous save.
  if (a.longPress && app.screen() == Screen::Running) setMotor(0);
  const auto& touch = M5.Touch.getDetail();
  if (touch.wasPressed()) touchScreen = app.screen();
  if (touch.isPressed()) lastActivity = now;
  app.update(now,{a.shortPress,a.longPress,b.shortPress});
  const bool released = touch.wasReleased() && touchScreen == app.screen();
  if (released) app.releaseTouch(touch.base_x, touch.base_y, touch.x, touch.y, now);
  setMotor(app.motor(monotonic()));
  const uint8_t wantedBrightness = now-lastActivity >= 30000 ? 35 : 160;
  if (wantedBrightness != brightness) {
    brightness = wantedBrightness;
    M5.Display.setBrightness(brightness);
  }
  if (action || released || app.needsTimeRefresh(lastFrame, now)) {
    render(now);
    lastFrame = now;
  }
  // No delay(), sleep or blocking vibration sequence: input/timers keep running.
}
