#include "App.h"
#include "UsbCommands.h"
#include "../StopWatchTourGuide/CourseConfig.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace tour;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x); } while(false)
constexpr Input next{true,false,false}, finish{false,true,false}, question{false,false,true};
LocalSeconds timeAt(int h,int m,int s=0) { return toSeconds({2026,11,20,h,m,s}); }
Tour started(int hour=14,int minute=0) {
  Tour t;
  CHECK(t.start(configuredCourse(),timeAt(hour,minute),true,timeAt(14,15),0)==StartError::None);
  return t;
}
struct FakeBlobs : BlobStore {
  std::array<std::vector<uint8_t>,2> data;
  int failWrite = -1, failRead = -1;
  size_t truncate = SIZE_MAX;
  ReadResult read(unsigned slot,std::vector<uint8_t>& out) override {
    if (int(slot)==failRead) return ReadResult::Error;
    if (data[slot].empty()) return ReadResult::Missing;
    out=data[slot]; return ReadResult::Ok;
  }
  bool write(unsigned slot,const std::vector<uint8_t>& in) override {
    if (int(slot)==failWrite) return false;
    data[slot]=std::vector<uint8_t>(in.begin(),in.begin()+std::min(truncate,in.size()));
    return truncate>=in.size();
  }
};
struct FakeEditor : CourseEditor {
  GuideSettings saved;
  bool available = true, opened = false;
  LoadResult load(GuideSettings& settings) override {
    if (saved.course.segments.empty()) return LoadResult::Empty;
    settings = saved;
    return LoadResult::Found;
  }
  bool open(const GuideSettings&, WifiAccess& access) override {
    if (!available) return false;
    opened = true;
    access = {"M5StopWatch-AB12", "12345678", "http://192.168.4.1"};
    return true;
  }
  void close(GuideSettings& settings) override { load(settings); opened = false; }
  bool writeCourse(std::string_view text) override { return available && parseSettings(text, saved); }
};
struct FakeClock : Clock {
  LocalSeconds now=timeAt(14,0);
  bool valid=true, failSet=false;
  bool read(LocalSeconds& result) override { result=now; return valid; }
  bool set(const DateTime& d) override {
    if (failSet || !validDate(d)) return false;
    now=toSeconds(d); valid=true; return true;
  }
};
Record completed(Millis elapsed=900000) {
  auto t=started(); t.update(elapsed,finish); return t.record();
}
bool hasText(const View& view,const std::string& text) {
  for (const auto& row:view.rows) if (row.text.find(text)!=std::string::npos) return true;
  return false;
}
void tapAction(App& app, Action action, Millis now) {
  for (const auto& box : app.view(now).boxes) {
    if (box.action == action) {
      app.tap(box.x + box.width / 2, box.y + box.height / 2, now);
      return;
    }
  }
  throw std::runtime_error("Requested on-screen action was missing");
}
void confirmDeadline(App& app,Millis now=0) {
  app.tap(233,190,now);
  CHECK(app.screen()==Screen::EditDeadline);
  app.tap(300,360,now);
  CHECK(app.screen()==Screen::Standby);
  CHECK(app.deadlineConfirmed());
}
std::string sendUsb(UsbCommands& usb, App& app, std::string_view bytes) {
  std::string response;
  for (char byte : bytes) response += usb.receive(byte, app);
  return response;
}
void usbClockSync() {
  FakeBlobs blobs;
  RecordStore store(blobs);
  struct ReadbackClock : FakeClock {
    int readOffset = 0;
    bool read(LocalSeconds& result) override {
      result = now + readOffset;
      return valid;
    }
  } rtc;
  rtc.valid = false;
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor);
  app.begin();
  UsbCommands usb;
  auto send = [&](const std::string& bytes) {
    return sendUsb(usb, app, bytes);
  };
  CHECK(send("TOUR TI").empty());
  CHECK(send("ME?\r\n") == "TOUR TIME READY");
  CHECK(send("unrelated log\n").empty());
  CHECK(send("TOUR TIME " + std::string(70, '0') + "\n").empty());
  CHECK(send("TOUR TIME 1x\n") == "TOUR TIME INVALID");
  CHECK(send("TOUR TIME 9999999999\n") == "TOUR TIME FAILED");
  CHECK(!rtc.valid);

  const auto leapDay = toSeconds({2028,2,29,23,59,47});
  auto setTime = [&](LocalSeconds seconds) {
    return send("TOUR TIME " + std::to_string(seconds) + "\n");
  };
  auto ok = [](LocalSeconds seconds) { return "TOUR TIME OK " + std::to_string(seconds); };
  rtc.failSet = true;
  CHECK(setTime(leapDay) == "TOUR TIME FAILED");
  rtc.failSet = false;
  rtc.readOffset = 5;
  CHECK(setTime(leapDay) == "TOUR TIME FAILED");
  rtc.readOffset = 1;
  CHECK(setTime(leapDay) == ok(leapDay + 1));
  CHECK(rtc.now == leapDay && !app.deadlineConfirmed());
  CHECK(hasText(app.view(0),"2028/03/01") && hasText(app.view(0),"00:14"));
  tapAction(app, Action::Clock, 0);
  CHECK(send("TOUR TIME?\n") == "TOUR TIME BUSY");
  CHECK(setTime(0) == "TOUR TIME BUSY" && rtc.now == leapDay);
  tapAction(app, Action::Back, 0);
  confirmDeadline(app);
  rtc.readOffset = 0;
  CHECK(setTime(leapDay + 300) == ok(leapDay + 300));
  CHECK(app.deadlineConfirmed() && hasText(app.view(0),"00:14"));
  const auto later = leapDay + 1200;
  CHECK(setTime(later) == ok(later));
  CHECK(!app.deadlineConfirmed() && hasText(app.view(0),"00:34"));
  confirmDeadline(app);
  tapAction(app, Action::Start, 0);
  CHECK(app.screen() == Screen::Running);
  CHECK(setTime(0) == "TOUR TIME BUSY" && rtc.now == later);
  CHECK(app.tour().record().started == later);
}
void lateStart() {
  auto t=started(14,2);
  CHECK(t.remainingMs(0)==780000);
  CHECK(t.segment()==0);
  CHECK(t.segmentBudgetMs()==104000);
  CHECK(t.update(73999).notice==Notice::None);
  CHECK(t.update(74000).notice==Notice::SegmentWarning);
  CHECK(t.update(660000).notice==Notice::OverallWarning);
  CHECK(t.update(660001).notice==Notice::None);
  CHECK(t.update(780000).notice==Notice::Deadline);
  CHECK(t.update(900000).notice==Notice::None);
}
void proportionalSegments() {
  struct Example { Millis entry; int64_t budget; const char* label; };
  for (const Example ex : {Example{60000,452000,"07:32"}, {120000,420000,"07:00"},
                           {150000,404000,"06:44"}, {180000,388000,"06:28"}}) {
    FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc; FakeEditor editor;
    App app(configuredCourse(), rtc, store, editor); app.begin(); confirmDeadline(app);
    tapAction(app, Action::Start, 0);
    tapAction(app, Action::NextSegment, ex.entry);
    CHECK(app.tour().remainingMs(ex.entry) == int64_t(900000 - ex.entry));
    CHECK(app.tour().segmentRemainingMs(ex.entry) == ex.budget);
    CHECK(hasText(app.view(ex.entry), std::string("今回の目安  ") + ex.label));
    CHECK(app.view(ex.entry).segmentRemainingFraction == 1.0f);
    CHECK(app.view(ex.entry + ex.budget / 2).segmentRemainingFraction == 0.5f);
    const Millis warning = ex.entry + ex.budget - 30000;
    app.update(warning - 1); CHECK(app.motor(warning - 1) == 0);
    app.update(warning); CHECK(app.motor(warning) == 220);
    const Millis expiry = ex.entry + ex.budget;
    app.update(expiry - 1); CHECK(app.motor(expiry - 1) == 0);
    app.update(expiry); CHECK(app.motor(expiry) == 220);
    CHECK(app.view(expiry).segmentRemainingFraction == 0.0f);
    app.update(expiry + 720); CHECK(app.motor(expiry + 720) == 0);
    CHECK(app.tour().record().course.segments[1].seconds == 420);
  }
}
void budgetAtDeadline() {
  auto t = started();
  t.update(899875, next);
  CHECK(t.segmentBudgetMs() == 125);
  CHECK(t.update(900000, next).notice == Notice::Deadline);
  CHECK(t.segmentBudgetMs() == 0 && t.segmentRemainingMs(900000) == 0);
  CHECK(t.update(901000, next).notice == Notice::Confirm);
  CHECK(t.update(901500).notice == Notice::None);
  CHECK(t.segmentBudgetMs() == 0 && t.segmentRemainingMs(901500) == -500);

  Course large{"長期", std::vector<Segment>(kMaxSegments, {"展示", INT32_MAX})};
  const auto start = toSeconds({2000,1,1,0,0,0});
  const auto end = toSeconds({2099,12,31,23,59,59});
  Tour longTour;
  CHECK(longTour.start(large,start,true,end,0)==StartError::None);
  CHECK(longTour.segmentBudgetMs() == ((end-start+16)/32)*1000);
}
void recoverTime() {
  auto t=started();
  t.update(120000,next); t.update(600000,next); t.update(780000,next);
  CHECK(t.update(900000,finish).notice==Notice::None);
  const auto& r=t.record();
  CHECK(r.segments[2].end-r.segments[2].start==180000);
  CHECK(int64_t(r.segments[2].end-r.segments[2].start)-int64_t(r.course.segments[2].seconds)*1000==-60000);
  CHECK(r.segments[3].end-r.segments[3].start==120000);
  CHECK(r.deadlineDifferenceMs()==0);
  CHECK(validRecord(r));
}
void simultaneousWarnings() {
  auto t=started();
  t.update(120000,next); t.update(630000,next);
  CHECK(t.update(780000).notice==Notice::OverallWarning);
  CHECK(t.update(780001).notice==Notice::None);
  CHECK(t.update(810000).notice==Notice::SegmentExpired);
  CHECK(t.update(810001).notice==Notice::None);
}
void imminentStart() {
  auto t=started(14,14);
  CHECK(t.remainingMs(0)==60000);
  CHECK(t.update(0).notice==Notice::OverallWarning);
  CHECK(t.update(1,question).notice==Notice::Confirm);
  CHECK(t.record().questions[0].segment==0);
  CHECK(t.update(8000).notice==Notice::SegmentExpired); // Too short for an advance warning.
  CHECK(t.update(59999).notice==Notice::None);
  CHECK(t.update(60000).notice==Notice::Deadline);
}
void overtime() {
  auto t=started(14,2);
  CHECK(t.update(780000).notice==Notice::Deadline);
  CHECK(t.remainingMs(781000)==-1000);
  t.update(840000,finish);
  CHECK(t.record().elapsed==840000);
  CHECK(t.record().deadlineDifferenceMs()==60000);
  CHECK(t.record().ended()==timeAt(14,16));
}
void questionsDuringVibration() {
  auto t=started(); Haptics h;
  t.update(120000,next);
  h.request(t.update(510000).notice,510000);
  CHECK(h.output(510010)!=0);
  h.request(t.update(510010,question).notice,510010);
  t.update(520000,question);
  CHECK(t.record().questions.size()==2);
  CHECK(t.record().questions[0].segment==1 && t.record().questions[1].segment==1);
  CHECK(t.record().questions[0].elapsed==510010);
  CHECK(t.record().questions[1].elapsed==520000);
  CHECK(t.remainingMs(520000)==380000);
  CHECK(t.segmentRemainingMs(520000)==20000);
}
void earlyFinish() {
  auto t=started(); t.update(120000,next);
  CHECK(t.update(600000,finish).finished);
  CHECK(t.record().elapsed==600000);
  CHECK(t.record().deadlineDifferenceMs()==-300000);
  CHECK(t.record().segments[0].end==120000);
  CHECK(t.record().segments[1].end-t.record().segments[1].start==480000);
  CHECK(!t.record().segments[2].visited && !t.record().segments[3].visited);
  CHECK(t.update(900000).notice==Notice::None);
  CHECK(validRecord(t.record()));
}
void midnightAndInvalidSettings() {
  Tour t;
  const auto now=timeAt(23,58), end=toSeconds({2026,11,21,0,5,0});
  CHECK(t.start(configuredCourse(),now,true,end,0)==StartError::None);
  CHECK(t.remainingMs(0)==420000);
  CHECK(t.update(300000).notice==Notice::OverallWarning);
  t.update(420000,finish);
  CHECK(t.record().ended()==end);
  CHECK(t.start(configuredCourse(),now,true,timeAt(0,5),0)==StartError::InvalidDeadline);
  CHECK(t.start(configuredCourse(),now,true,now,0)==StartError::InvalidDeadline);
  CHECK(t.start(configuredCourse(),now,false,end,0)==StartError::ClockUnset);
  CHECK(t.start({"empty",{}},now,true,end,0)==StartError::InvalidCourse);
  CHECK(t.start({"zero",{{"bad",0}}},now,true,end,0)==StartError::InvalidCourse);
  CHECK(t.start({"negative",{{"bad",-1}}},now,true,end,0)==StartError::InvalidCourse);
}
void persistenceAndReboot() {
  FakeBlobs blobs;
  { RecordStore store(blobs); CHECK(store.save(completed())); }
  Record loaded;
  { RecordStore afterBoot(blobs); CHECK(afterBoot.load(loaded)==LoadResult::Found); }
  CHECK(loaded.elapsed==900000 && loaded.course.name=="施設案内");
  { auto second=started(); second.update(120000,next); second.update(130000,question); }
  RecordStore reboot(blobs);
  CHECK(reboot.load(loaded)==LoadResult::Found);
  CHECK(loaded.elapsed==900000 && loaded.questions.empty());
  FakeClock rtc; rtc.now=timeAt(15,0);
  FakeEditor editor;
  App app(configuredCourse(), rtc, reboot, editor); app.begin();
  CHECK(app.screen()==Screen::Standby);
  app.update(0,{false,false,true});
  CHECK(app.screen()==Screen::Review);
  CHECK(hasText(app.view(0),"14:15:00"));
}
void simultaneousInputs() {
  auto t=started(); t.update(120000,next);
  t.update(500000,{true,false,true});
  CHECK(t.segment()==2);
  CHECK(t.record().questions.back().segment==1);
  t.update(550000,{true,true,true});
  CHECK(!t.running());
  CHECK(t.record().questions.back().segment==2);
  CHECK(!t.record().segments[3].visited);
}
void longPressAndBounce() {
  Button b;
  CHECK(!b.update(true,0).longPress);
  b.update(false,5); b.update(true,10); b.update(false,16); b.update(true,20);
  CHECK(!b.update(true,45).shortPress);
  CHECK(!b.update(true,819).longPress);
  CHECK(b.update(true,820).longPress);
  CHECK(!b.update(true,900).longPress);
  CHECK(!b.update(false,1000).shortPress);
  CHECK(!b.update(false,1025).shortPress);
  b.update(true,1100); b.update(true,1125); b.update(false,1200);
  CHECK(b.update(false,1225).shortPress);
  CHECK(!b.update(false,1300).shortPress);
  Button delayed;
  delayed.update(true,0); delayed.update(true,25); delayed.update(false,900);
  CHECK(delayed.update(false,925).longPress);
  Button shortB(false);
  shortB.update(true,0); shortB.update(true,25);
  CHECK(!shortB.update(true,5000).longPress);
  shortB.update(false,5100); CHECK(shortB.update(false,5125).shortPress);
}
void holdAcrossScreens() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc;
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin(); confirmDeadline(app);
  Button b;
  for (Millis now=0;now<=2000;now+=25) {
    const auto event=b.update(now<1500,now);
    app.update(now,{event.shortPress,event.longPress,false});
  }
  CHECK(app.screen()==Screen::Running && app.tour().segment()==0);
  for (Millis now=2100;now<=4000;now+=25) {
    const auto event=b.update(now<3800,now);
    app.update(now,{event.shortPress,event.longPress,false});
  }
  CHECK(app.screen()==Screen::Review);
  CHECK(!app.unsaved());
  app.update(4100,question); // B returns to standby.
  CHECK(app.screen()==Screen::Standby && !app.deadlineConfirmed());
}
void delayAndPriority() {
  auto t=started();
  CHECK(t.update(950000).notice==Notice::Deadline);
  CHECK(t.update(950001).notice==Notice::None);
  auto switched=started();
  CHECK(switched.update(90000,next).notice==Notice::Confirm); // Discard old section warning.
  CHECK(switched.update(90001).notice==Notice::None);
  auto stopped=started();
  CHECK(stopped.update(900000,finish).notice==Notice::None);
  auto overlap=started();
  CHECK(overlap.update(780000,question).notice==Notice::OverallWarning);
  CHECK(overlap.record().questions.size()==1);
}
void segmentExpiryPriority() {
  auto t = started();
  CHECK(t.update(120250, question).notice == Notice::SegmentExpired); // Missed the advance warning.
  CHECK(t.record().questions.size() == 1 && t.segment() == 0);
  CHECK(t.update(120251).notice == Notice::None);
  auto switched = started();
  CHECK(switched.update(120000, next).notice == Notice::Confirm);
  auto finished = started();
  CHECK(finished.update(120000, finish).notice == Notice::None);

  Tour coincident;
  CHECK(coincident.start({"案内", {{"展示", 780}, {"移動", 120}}},
                         timeAt(14,0), true, timeAt(14,15), 0) == StartError::None);
  CHECK(coincident.update(780000).notice == Notice::OverallWarning);
  CHECK(coincident.update(780600).notice == Notice::None);
}
void shortAndLastSegments() {
  Tour t;
  CHECK(t.start({"short",{{"short",30},{"tiny",1}}},timeAt(14,0),true,timeAt(14,0,31),0,{1,30})==StartError::None);
  CHECK(t.update(20000).notice==Notice::None);
  t.update(20001,next);
  CHECK(t.segmentRemainingMs(20001)==10999);
  CHECK(t.update(29999).notice==Notice::None);
  CHECK(t.update(30000).notice==Notice::OverallWarning);
  CHECK(t.update(31000,next).lastSegmentHint);
  CHECK(t.running() && t.segment()==1);
}
void hapticPatterns() {
  Haptics h;
  h.request(Notice::Confirm,0);
  CHECK(h.output(79)>0); CHECK(h.output(80)==0);
  h.request(Notice::SegmentWarning,100);
  CHECK(h.output(399)>0); CHECK(h.output(400)==0);
  h.request(Notice::OverallWarning,500);
  CHECK(h.output(699)>0); CHECK(h.output(700)==0);
  CHECK(h.output(849)==0); CHECK(h.output(850)>0);
  CHECK(h.output(1049)>0); CHECK(h.output(1050)==0);
  h.request(Notice::Deadline,1100);
  CHECK(h.output(1699)>0); CHECK(h.output(1700)==0);
  h.request(Notice::SegmentWarning,1800);
  h.request(Notice::OverallWarning,1810); // Preempts the old pattern immediately.
  h.request(Notice::Confirm,1820); h.request(Notice::Confirm,1830);
  CHECK(h.output(2010)==0);
  CHECK(h.output(2160)>0);
  CHECK(h.output(2360)>0); CHECK(h.output(2440)>0); CHECK(h.output(2520)==0);
  h.request(Notice::Deadline,2600); h.request(Notice::SegmentWarning,2610);
  CHECK(h.output(3200)==0); // No stale warning queued.
  h.request(Notice::SegmentExpired,4000);
  for (Millis pulse : {4000, 4280, 4560}) {
    CHECK(h.output(pulse) == 220 && h.output(pulse + 159) == 220);
    CHECK(h.output(pulse + 160) == 0 && h.output(pulse + 279) == 0);
  }
  h.request(Notice::OverallWarning,5000); h.request(Notice::SegmentExpired,5010);
  CHECK(h.output(5200)==0 && h.output(5350)==220 && h.output(5550)==0);
  h.request(Notice::SegmentExpired,5600);
  h.stop(); CHECK(h.output(5601)==0);
}
void storageFailuresAndCorruption() {
  FakeBlobs blobs; RecordStore store(blobs);
  Record loaded;
  CHECK(store.load(loaded)==LoadResult::Empty);
  CHECK(store.save(completed()));
  const auto good=blobs.data[0];
  blobs.failWrite=1;
  CHECK(!store.save(completed(800000)));
  CHECK(blobs.data[0]==good);
  CHECK(store.load(loaded)==LoadResult::Found && loaded.elapsed==900000);
  blobs.failWrite=-1;
  const auto second=encodeRecord(completed(800000),2);
  // Every possible interrupted-write length must retain the first record.
  for (size_t n=0;n<second.size();++n) {
    blobs.truncate=n;
    CHECK(!store.save(completed(800000)));
    CHECK(store.load(loaded)==LoadResult::Found && loaded.elapsed==900000);
    CHECK(blobs.data[0]==good);
  }
  blobs.truncate=SIZE_MAX;
  CHECK(store.save(completed(800000)));
  CHECK(store.load(loaded)==LoadResult::Found && loaded.elapsed==800000);
  blobs.data[1][20]^=1;
  CHECK(store.load(loaded)==LoadResult::Found && loaded.elapsed==900000);
  blobs.failRead=1;
  CHECK(!store.save(completed(700000)));
  CHECK(store.load(loaded)==LoadResult::IoError);
  blobs.failRead=-1;
  blobs.data[0][0]^=1;
  CHECK(store.load(loaded)==LoadResult::Damaged);
}
void recordValidation() {
  auto r=completed();
  uint64_t generation=0;
  Record decoded;
  auto bytes=encodeRecord(r,42);
  CHECK(decodeRecord(bytes,decoded,generation)); CHECK(generation==42);
  for (size_t i=0;i<bytes.size();++i) {
    auto broken=bytes; broken[i]^=1;
    CHECK(!decodeRecord(broken,decoded,generation));
  }
  bytes.push_back(0); CHECK(!decodeRecord(bytes,decoded,generation));
  r.segments[1]={true,10,20}; CHECK(!validRecord(r));
  r=completed(); r.questions.push_back({2,1000}); CHECK(!validRecord(r));
  r=completed(); r.version=999; CHECK(encodeRecord(r,1).empty());
  r=completed(); r.elapsed=UINT64_MAX; CHECK(!validRecord(r));
  r=completed(0); CHECK(validRecord(r)); // A genuine immediate finish is valid.
}
void retrySaveOnScreen() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc;
  CHECK(store.save(completed()));
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin(); confirmDeadline(app);
  app.update(0,finish); CHECK(app.screen()==Screen::Running);
  blobs.failWrite=1;
  app.update(600000,finish);
  CHECK(app.unsaved() && app.screen()==Screen::Review);
  CHECK(hasText(app.view(600000),"再保存"));
  app.update(600001,question);
  CHECK(app.screen()==Screen::Review);
  Record old; CHECK(store.load(old)==LoadResult::Found && old.elapsed==900000);
  blobs.failWrite=-1;
  tapAction(app,Action::Retry,600002);
  CHECK(!app.unsaved());
  app.update(600003,question); CHECK(app.screen()==Screen::Standby);
  CHECK(store.load(old)==LoadResult::Found && old.elapsed==600000);
}
void dateEditorAndReview() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc; rtc.valid=false;
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin();
  app.update(0,finish); CHECK(app.screen()==Screen::Standby);
  app.tap(233,130,1); CHECK(app.screen()==Screen::EditClock);
  rtc.failSet=true; app.tap(300,360,2); CHECK(app.screen()==Screen::EditClock);
  rtc.failSet=false; app.tap(300,360,3); CHECK(app.screen()==Screen::Standby);
  confirmDeadline(app,4);
  app.update(10,finish); CHECK(app.screen()==Screen::Running);
  for (int i=1;i<=7;++i) app.update(1000*i,question);
  app.update(10000,finish); CHECK(app.screen()==Screen::Review);
  app.update(10001,next);
  CHECK(hasText(app.view(10001),"1.")); CHECK(!hasText(app.view(10001),"4."));
  app.swipe(-80); CHECK(hasText(app.view(10002),"4."));
  app.swipe(-80); CHECK(hasText(app.view(10003),"7."));
  app.swipe(80); CHECK(hasText(app.view(10004),"4."));
  app.update(10005,next); CHECK(hasText(app.view(10005),"未実施"));
  app.update(10006,next); app.update(10007,next); app.update(10008,next);
  CHECK(hasText(app.view(10008),"全体の振り返り"));
}
void calendarAndDuration() {
  for (int y=2000;y<=2099;++y) for (int m=1;m<=12;++m) for(int d=1;d<=31;++d) {
    DateTime date{y,m,d,23,59,59};
    if (!validDate(date)) continue;
    const auto back=fromSeconds(toSeconds(date));
    CHECK(back.year==y && back.month==m && back.day==d && back.hour==23 && back.minute==59 && back.second==59);
  }
  CHECK(validDate({2024,2,29,0,0,0})); CHECK(!validDate({2026,2,29,0,0,0}));
  CHECK(!validDate({2100,1,1,0,0,0}));
  CHECK(duration(-1000)=="00:01"); CHECK(duration(6000000)=="100:00");
  CHECK(difference(-60000)=="-01:00");
}
void deadlineEditorMidnight() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc;
  rtc.now=timeAt(23,58);
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin();
  app.tap(233,190,0); // Default next day 00:13; edit to 00:05.
  CHECK(hasText(app.view(0),"2026/11/21"));
  tapAction(app,Action::Minute,0);
  for (int i=0;i<8;++i) tapAction(app,Action::PreviousValue,0);
  CHECK(hasText(app.view(0),"00:05"));
  app.tap(300,360,0); CHECK(app.deadlineConfirmed());
  app.update(0,finish); CHECK(app.tour().remainingMs(0)==420000);
  CHECK(hasText(app.view(1),"07:00")); // Do not lose a second on the first frame.
  CHECK(hasText(app.view(1000),"06:59"));
  app.update(420000); CHECK(hasText(app.view(420000),"終了時刻"));
  CHECK(hasText(app.view(421000),"終了超過"));
  CHECK(hasText(app.view(421000),"00:01"));
  App past(configuredCourse(),rtc,store,editor); past.begin();
  past.tap(233,190,0); tapAction(past,Action::Day,0); tapAction(past,Action::PreviousValue,0); // next day's date -> today
  past.tap(300,360,0);
  CHECK(past.screen()==Screen::EditDeadline && !past.deadlineConfirmed());
  CHECK(hasText(past.view(0),"未来に直してください"));
  past.tap(150,360,0); CHECK(past.screen()==Screen::Standby);
}
void reviewSecondPrecision() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc;
  CHECK(store.save(completed(119999)));
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin(); app.update(0,question);
  CHECK(hasText(app.view(0),"14:01:59"));
  CHECK(hasText(app.view(0),"予定より早い 13:01"));
  app.update(0,next);
  CHECK(hasText(app.view(0),"実績 01:59 / 差 -00:01"));
}
void cancelPreviousSegmentVibration() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc;
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin(); confirmDeadline(app);
  app.update(0,finish);
  app.update(90000); CHECK(app.motor(90000)==220);
  app.update(90010,next); CHECK(app.motor(90010)==150);
  CHECK(app.motor(90090)==0);
  const Millis expiry = 90010 + app.tour().segmentBudgetMs();
  app.update(expiry); CHECK(app.motor(expiry) == 220);
  tapAction(app, Action::NextSegment, expiry + 100);
  CHECK(app.motor(expiry + 100) == 150 && app.motor(expiry + 280) == 0);
  app.update(780000); CHECK(app.motor(780000)==220);
  app.update(780010,next); // Global warning must keep playing across transitions.
  CHECK(app.motor(780010)==220);
  app.update(780020,finish); CHECK(app.motor(780020)==0);
}
void longUptimeAndLimits() {
  Tour t;
  const Millis origin=(Millis(1)<<32)-100;
  CHECK(t.start(configuredCourse(),timeAt(14,0),true,timeAt(14,15),origin)==StartError::None);
  CHECK(t.remainingMs(origin+200000)==700000);
  for (size_t i=0;i<kMaxQuestions;++i) CHECK(!t.update(origin+i,question).questionFull);
  CHECK(t.update(origin+kMaxQuestions,question).questionFull);
  CHECK(t.record().questions.size()==kMaxQuestions);
  t.update(origin+900000,finish);
  CHECK(validRecord(t.record()));
  auto bytes=encodeRecord(t.record(),1); CHECK(!bytes.empty() && bytes.size()<kMaxRecordBytes);
}
void touchControls() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc;
  rtc.valid = false;
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin();
  app.tap(233,302,0); CHECK(app.screen() == Screen::EditClock);
  tapAction(app,Action::Save,1);
  app.tap(233,302,2); CHECK(app.screen() == Screen::EditDeadline);
  tapAction(app,Action::Save,3); CHECK(app.deadlineConfirmed());
  app.tap(69,271,4); // Outside the visible corner of the Start capsule.
  CHECK(app.screen() == Screen::Standby);
  app.tap(233,302,5); CHECK(app.screen() == Screen::Running);
  app.tap(310,360,50); app.tap(146,360,100);
  CHECK(app.tour().record().questions.size() == 1);
  CHECK(app.tour().record().questions[0].segment == 0 && app.tour().segment() == 1);
  app.tap(146,360,200); app.tap(146,360,300);
  CHECK(app.tour().segment() == 3);
  app.tap(73,334,400); CHECK(app.screen() == Screen::Running);
  app.update(1000,finish); CHECK(app.screen() == Screen::Review);
  app.tap(311,378,1001); CHECK(hasText(app.view(1001),"導入"));
  app.tap(153,378,1002); CHECK(app.screen() == Screen::Standby);
}
void footerTaps() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc;
  CHECK(store.save(completed()));
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin();
  CHECK(!app.needsTimeRefresh(0, 999) && app.needsTimeRefresh(0, 1000));

  // A finger can roll on the lower edge of a button without leaving it.
  app.releaseTouch(143,403,173,402,1001);
  CHECK(app.screen() == Screen::CoursePlan && !app.needsTimeRefresh(1001,5000));
  app.releaseTouch(160,380,161,312,5001);
  CHECK(app.screen() == Screen::CoursePlan && hasText(app.view(5001),"移動"));
  app.releaseTouch(148,409,174,406,5002);
  CHECK(app.screen() == Screen::Standby);
  app.releaseTouch(298,403,331,403,5003);
  CHECK(app.screen() == Screen::Review && !app.needsTimeRefresh(5003,9000));
  app.releaseTouch(150,409,177,405,9001);
  CHECK(app.screen() == Screen::Standby);

  const auto original = rtc.now;
  auto editMinute = [&](Millis now) {
    tapAction(app,Action::Clock,now);
    tapAction(app,Action::Minute,now);
    tapAction(app,Action::NextValue,now);
  };
  editMinute(9002);
  app.releaseTouch(214,369,241,369,9003); // Cross from Back into Save.
  CHECK(app.screen() == Screen::EditClock && rtc.now == original);
  app.releaseTouch(148,396,172,398,9004);
  CHECK(app.screen() == Screen::Standby && rtc.now == original);
  editMinute(9005);
  rtc.failSet = true;
  app.releaseTouch(303,398,332,397,9010);
  CHECK(app.screen() == Screen::EditClock && hasText(app.view(9010),"時計を設定できません"));
  CHECK(!app.needsTimeRefresh(9010,13009) && app.needsTimeRefresh(9010,13010));
  CHECK(!app.needsTimeRefresh(13010,14000) && !hasText(app.view(13010),"時計を設定できません"));
  rtc.failSet = false;
  app.releaseTouch(303,398,332,397,14001);
  CHECK(app.screen() == Screen::Standby && rtc.now == original + 60);
}
void wifiSettingsValidation() {
  GuideSettings original{configuredCourse(), {}}, decoded;
  std::string name;
  for (int i = 0; i < 32; ++i) name += "あ";
  GuideSettings largest{{name, std::vector<Segment>(32, {name, INT32_MAX})}, {INT32_MAX, INT32_MAX}};
  const auto full = settingsText(largest);
  CHECK(parseSettings(full, decoded) && settingsText(decoded) == full);
  CHECK(parseSettings(settingsText(original), decoded));
  const auto good = settingsText(decoded);
  for (const auto& body : std::vector<std::string>{
      "", "名前", "名前\n120\t30", "名前\n120\t30\n", "名前\n120\t30\n1\t",
      "名前\n-1\t30\n1\t区間", "名前\n120\t0\n1\t区間", "名前\n120\t30\n2.5\t区間",
      "名前\n120\t30\n2147483648\t区間", full + "\n1\t超過", name + "x" + good,
      good + "\xC0\xAF", good + "\xED\xA0\x80", good + std::string(1, '\0'), good + "\t",
      good + "\n0\t区間", good + "\n1\t  ", good + std::string(kMaxCourseBytes, 'x')}) {
    CHECK(!parseSettings(body, decoded));
    CHECK(settingsText(decoded) == good);
  }
}
void usbCourseSettings() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc; FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin();
  UsbCommands usb;
  auto command = [&](const std::string& value) {
    return sendUsb(usb, app, "TOUR COURSE " + value + "\n");
  };
  const std::string body = "夜の案内\n60\t10\n300\t見学";
  const std::string length = std::to_string(body.size());
  auto stage = [&] {
    CHECK(command("BEGIN") == "TOUR COURSE READY");
    for (const auto* line : {"夜の案内", "60\t10", "300\t見学"})
      CHECK(command(std::string("LINE ") + line) == "TOUR COURSE NEXT");
  };
  const auto deadline = toSeconds({2026,11,21,0,15,0});
  const std::string save = "SAVE " + std::to_string(deadline) + " " + length;
  stage();
  CHECK(command("SAVE - 1") == "TOUR COURSE INVALID");
  CHECK(command(save) == "TOUR COURSE INVALID");
  stage();
  CHECK(command("LINE " + std::string(150, 'x')) == "TOUR COURSE INVALID");
  CHECK(command(save) == "TOUR COURSE INVALID");
  stage();
  tapAction(app, Action::Course, 0);
  tapAction(app, Action::WifiEdit, 0);
  CHECK(command(save) == "TOUR COURSE BUSY");
  tapAction(app, Action::Back, 0);
  tapAction(app, Action::Back, 0);
  CHECK(command(save) == "TOUR COURSE INVALID");
  for (const auto invalid : {rtc.now - 60, rtc.now, deadline + 1, toSeconds({2100,1,1,0,0,0})}) {
    stage();
    CHECK(command("SAVE " + std::to_string(invalid) + " " + length) == "TOUR COURSE DEADLINE");
  }
  rtc.valid = false;
  stage(); CHECK(command(save) == "TOUR COURSE CLOCK");
  rtc.valid = true;
  editor.available = false;
  stage(); CHECK(command(save) == "TOUR COURSE FAILED");
  CHECK(!app.deadlineConfirmed() && hasText(app.view(0), "施設案内") && editor.saved.course.name.empty());
  editor.available = true;
  stage(); CHECK(command(save) == "TOUR COURSE OK");
  CHECK(settingsText(editor.saved) == body && app.deadlineConfirmed());
  CHECK(hasText(app.view(0), "2026/11/21") && hasText(app.view(0), "00:15"));
  stage(); CHECK(command("SAVE - " + length) == "TOUR COURSE OK");
  tapAction(app, Action::Start, 0);
  CHECK(app.tour().record().deadline == deadline && app.tour().record().course.name == "夜の案内");
  CHECK(command("BEGIN") == "TOUR COURSE BUSY");
  App reboot(configuredCourse(), rtc, store, editor); reboot.begin();
  CHECK(hasText(reboot.view(0), "夜の案内") && !reboot.deadlineConfirmed());
}
void wifiEditedTour() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc; FakeEditor editor;
  CHECK(store.save(completed()));
  App app(configuredCourse(), rtc, store, editor); app.begin();
  confirmDeadline(app);
  tapAction(app,Action::Course,1);
  editor.available = false;
  tapAction(app,Action::WifiEdit,2);
  CHECK(app.screen() == Screen::CoursePlan && hasText(app.view(2),"Wi-Fiを開始できません"));
  editor.available = true;
  tapAction(app,Action::WifiEdit,3);
  CHECK(editor.opened && hasText(app.view(3),"http://192.168.4.1"));
  app.update(4,finish); CHECK(app.screen() == Screen::WifiEdit && !app.tour().running());
  CHECK(parseSettings("特別案内\n30\t5\n20\t見学\n1\t解散", editor.saved));
  app.update(5,question); // B closes Wi-Fi and returns to the plan.
  CHECK(!editor.opened && hasText(app.view(5),"見学") && hasText(app.view(5),"00:20"));
  tapAction(app,Action::Back,6);
  tapAction(app,Action::Records,7);
  CHECK(hasText(app.view(7),"施設案内"));
  tapAction(app,Action::Back,8);
  tapAction(app,Action::Start,10);
  CHECK(app.tour().record().course.name == "特別案内" && app.tour().segmentRemainingMs(10) == 857000);
  app.update(852009); CHECK(app.motor(852009) == 0);
  app.update(852010); CHECK(app.motor(852010) == 220);
  CHECK(app.view(860010).ringInk == Ink::Accent);
  app.update(870010); CHECK(app.motor(870010) == 220 && app.view(870010).ringInk == Ink::Warning);
  App reboot(configuredCourse(), rtc, store, editor); reboot.begin();
  CHECK(hasText(reboot.view(0),"特別案内"));
  tapAction(reboot,Action::Course,0);
  CHECK(hasText(reboot.view(0),"解散") && hasText(reboot.view(0),"00:01"));
}
void snapshot(const std::string& name,const View& v) {
  std::ofstream out("build/"+name+".tsv");
  if (v.remainingPercent >= 0) out<<"ring\t"<<v.remainingPercent<<'\t'<<int(v.ringInk)<<'\n';
  if (v.segmentCount) out<<"segments\t"<<v.segmentIndex<<'\t'<<v.segmentCount<<'\t'<<v.segmentRemainingFraction<<'\n';
  for (const auto& b:v.boxes) out<<"box\t"<<b.x<<'\t'<<b.y<<'\t'<<b.width<<'\t'<<b.height<<'\t'<<int(b.ink)<<'\t'<<b.radius<<'\n';
  for (const auto& r:v.rows) out<<"text\t"<<r.x<<'\t'<<r.y<<'\t'<<r.size<<'\t'<<r.width<<'\t'<<int(r.ink)<<'\t'<<int(r.background)<<'\t'<<r.text<<'\n';
}
void currentSegmentRing() {
  for (const size_t count : {size_t{1}, kMaxSegments}) {
    FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc; FakeEditor editor;
    Course course{"案内", std::vector<Segment>(count, {"展示", 1})};
    App app(course, rtc, store, editor); app.begin();
    CHECK(app.view(0).segmentCount == 0);
    confirmDeadline(app);
    rtc.now = timeAt(14,15) - static_cast<int64_t>(count); // One second per segment.
    tapAction(app, Action::Start, 0);
    for (size_t i = 0; i < count; ++i) {
      const Millis start = i * 1000;
      const auto view = app.view(start);
      CHECK(view.segmentCount == count && view.segmentIndex == i);
      CHECK(view.segmentRemainingFraction == 1.0f);
      CHECK(app.view(start + 125).segmentRemainingFraction == 0.875f);
      CHECK(app.view(start + 500).segmentRemainingFraction == 0.5f);
      CHECK(app.view(start + 1000).segmentRemainingFraction == 0.0f);
      if (i + 1 < count) tapAction(app, Action::NextSegment, start + 1000);
    }
    app.update(901000, question);
    app.update(901001, next);
    CHECK(app.view(901001).segmentIndex == count - 1);
    app.update(901002, finish);
    CHECK(app.view(901002).segmentCount == 0);
  }
}
void uiSnapshots() {
  FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc;
  FakeEditor editor;
  App app(configuredCourse(), rtc, store, editor); app.begin();
  snapshot("standby",app.view(0));
  tapAction(app,Action::Course,0); snapshot("course",app.view(0));
  tapAction(app,Action::WifiEdit,0); snapshot("wifi",app.view(0));
  tapAction(app,Action::Back,0);
  app.swipe(-80); CHECK(hasText(app.view(0),"移動"));
  tapAction(app,Action::NextPage,0); CHECK(hasText(app.view(0),"導入"));
  tapAction(app,Action::Back,0);
  app.tap(233,190,0); snapshot("editor",app.view(0));
  tapAction(app,Action::Save,0); snapshot("ready",app.view(0));
  tapAction(app,Action::Start,0); CHECK(app.screen()==Screen::Running);
  tapAction(app,Action::NextSegment,120000); tapAction(app,Action::NextSegment,600000);
  const auto running = app.view(700000);
  CHECK(running.segmentRemainingFraction == 0.5f);
  snapshot("running",running);
  app.update(961000); snapshot("overtime",app.view(961000));
  tapAction(app,Action::Question,961000); app.update(962000,finish);
  snapshot("review",app.view(970000));
  app.update(970001,next); app.update(970002,next); app.update(970003,next);
  snapshot("questions",app.view(970003));
  CHECK(hasText(app.view(970003),"質問 1件"));
  tapAction(app,Action::Back,970004); CHECK(app.screen()==Screen::Standby);
}
int main(int argc, char** argv) {
  // Run the firmware's USB command loop against a pseudo-terminal for CLI tests.
  if (argc == 2 && std::string_view(argv[1]) == "--usb") {
    FakeBlobs blobs; RecordStore store(blobs); FakeClock rtc; FakeEditor editor;
    App app(configuredCourse(), rtc, store, editor); app.begin();
    UsbCommands usb;
    char byte;
    while (std::cin.get(byte)) {
      const auto reply = usb.receive(byte, app);
      if (!reply.empty()) std::cout << reply << std::endl;
    }
    return 0;
  }
  const std::pair<const char*,std::function<void()>> tests[]={
    {"遅れて開始",lateStart},{"区間の比例配分",proportionalSegments},{"締切前後と配分の上限",budgetAtDeadline},
    {"時間を取り戻す",recoverTime},
    {"区間予告と全体予告が同時",simultaneousWarnings},{"終了間際の開始",imminentStart},
    {"締切超過",overtime},{"質問と入力",questionsDuringVibration},{"途中で終了",earlyFinish},
    {"日またぎと不正設定",midnightAndInvalidSettings},{"保存と復帰",persistenceAndReboot},
    {"同時入力の帰属",simultaneousInputs},{"長押し・チャタリング",longPressAndBounce},
    {"画面遷移をまたぐ長押し",holdAcrossScreens},{"処理遅延・通知優先",delayAndPriority},
    {"区間超過の通知と優先順位",segmentExpiryPriority},
    {"短い区間・最終区間",shortAndLastSegments},{"振動パターンと割込み",hapticPatterns},
    {"保存失敗・全中断位置・破損",storageFailuresAndCorruption},{"レコードの検証",recordValidation},
    {"画面から再保存",retrySaveOnScreen},{"日時設定・振り返り操作",dateEditorAndReview},
    {"暦・時間表示",calendarAndDuration},{"日時編集の日またぎ・残秒表示",deadlineEditorMidnight},
    {"表示秒数と差の整合",reviewSecondPrecision},
    {"移動前の区間振動を破棄",cancelPreviousSegmentVibration},
    {"長時間稼働・記録上限",longUptimeAndLimits},
    {"画面上の操作と開始条件",touchControls},{"下段のタップ・スワイプ・描画更新",footerTaps},
    {"USB自動時刻設定",usbClockSync},{"Wi-Fi設定の上限と不正入力",wifiSettingsValidation},
    {"USBコース設定と終了日時の確定",usbCourseSettings},
    {"Wi-Fi設定から案内開始・再起動",wifiEditedTour},
    {"案内中の区間リング",currentSegmentRing},{"画面プレビュー出力",uiSnapshots}
  };
  size_t passed=0;
  for (const auto& t:tests) {
    try { t.second(); ++passed; std::cout<<"PASS "<<t.first<<'\n'; }
    catch (const std::exception& e) { std::cerr<<"FAIL "<<t.first<<"\n"<<e.what()<<'\n'; return 1; }
  }
  std::cout<<passed<<" tests passed\n";
}
