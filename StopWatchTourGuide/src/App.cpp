#include "App.h"
#include <algorithm>
#include <cstdlib>
#include <utility>

namespace tour {
namespace {
void row(View& v, const std::string& text, int y, int size = 24,
         Ink ink = Ink::Normal, int width = 350, int x = 233, Ink background = Ink::Black) {
  v.rows.push_back({text, x, y, size, width, ink, background});
}
void surface(View& v, int x, int y, int w, int h, Ink fill, Action action, int radius) {
  v.boxes.push_back({x, y, w, h, radius, fill, action});
}
void button(View& v, const std::string& label, Action action, int x, int y, int w, int h,
            Ink fill = Ink::Surface, int size = 24) {
  surface(v, x, y, w, h, fill, action, h / 2);
  row(v, label, y + h / 2, size, fill == Ink::Primary ? Ink::OnPrimary : Ink::Normal,
      w - 18, x + w / 2, fill);
}
std::string countText(size_t n) { return std::to_string(n); }
LocalSeconds suggestedDeadline(LocalSeconds now) {
  return std::min((now / 60 + 15) * 60, toSeconds({2099,12,31,23,59,0}));
}
bool contains(const Box& box, int x, int y) {
  if (x < box.x || x >= box.x + box.width || y < box.y || y >= box.y + box.height) return false;
  const int dx = std::max({box.x + box.radius - x, 0, x - (box.x + box.width - 1 - box.radius)});
  const int dy = std::max({box.y + box.radius - y, 0, y - (box.y + box.height - 1 - box.radius)});
  return dx * dx + dy * dy <= box.radius * box.radius;
}
}  // namespace
App::App(Course course, Clock& clock, RecordStore& store, CourseEditor& editor)
    : settings_{std::move(course), {}}, clock_(clock), store_(store), editor_(editor) {}
void App::begin() {
  screen_ = Screen::Standby;
  const auto loaded = store_.load(review_);
  haveReview_ = loaded == LoadResult::Found;
  if (loaded == LoadResult::Damaged) message_ = "保存記録が破損しています";
  if (loaded == LoadResult::IoError) message_ = "保存領域を読めません";
  const auto loadedSettings = editor_.load(settings_);
  if (loadedSettings == LoadResult::Damaged || loadedSettings == LoadResult::IoError)
    message_ = "コースを読めず初期配分を使用";
  messageUntil_ = 6000;
  LocalSeconds now = 0;
  if (clock_.read(now)) deadline_ = suggestedDeadline(now);
}
CourseSave App::saveCourse(std::string_view text, std::optional<LocalSeconds> deadline) {
  if (screen_ != Screen::Standby) return CourseSave::Failed;
  if (deadline) {
    LocalSeconds now;
    if (!clock_.read(now)) return CourseSave::ClockUnset;
    if (*deadline <= now || *deadline >= toSeconds({2100,1,1,0,0,0}) || *deadline % 60 != 0)
      return CourseSave::InvalidDeadline;
  }
  const auto result = editor_.save(text, settings_);
  if (result == CourseSave::Saved && deadline) {
    deadline_ = *deadline;
    deadlineConfirmed_ = true;
    message_.clear();
  }
  return result;
}
bool App::syncClock(LocalSeconds requested, LocalSeconds& verified) {
  if (screen_ != Screen::Standby || requested < 0 ||
      requested >= toSeconds({2100,1,1,0,0,0})) return false;
  if (!clock_.set(fromSeconds(requested)) || !clock_.read(verified) ||
      verified < requested || verified - requested > 1) return false;
  if (!deadlineConfirmed_ || deadline_ <= verified) {
    deadlineConfirmed_ = false;
    deadline_ = suggestedDeadline(verified);
  }
  return true;
}
void App::message(const std::string& text, Millis now) {
  message_ = text;
  messageUntil_ = now + 4000;
}
void App::saveResult(Millis now) {
  unsaved_ = !store_.save(review_);
  message(unsaved_ ? "保存できませんでした" : "保存しました", now);
}
void App::update(Millis now, Input input) {
  if (screen_ == Screen::Running) {
    const auto result = tour_.update(now, input);
    if (result.finished) {
      haptics_.stop();
      review_ = tour_.record();
      haveReview_ = true;
      reviewPage_ = questionOffset_ = 0;
      screen_ = Screen::Review;
      deadlineConfirmed_ = false;
      saveResult(now);
      return;
    }
    if (result.lastSegmentHint) message("Aを0.8秒長押しで終了", now);
    if (result.questionFull) message("質問記録が上限に達しました", now);
    if (result.notice == Notice::Deadline) deadlineBannerUntil_ = now + 3000;
    if (result.segmentChanged) haptics_.cancelSegmentNotices();
    haptics_.request(result.notice, now);
    return;
  }
  if (screen_ == Screen::Review) {
    if (input.aShort) {
      reviewPage_ = (reviewPage_ + 1) % (review_.segments.size() + 1);
      questionOffset_ = 0;
    }
    if (input.bShort) {
      if (unsaved_) message("未保存です。再保存してください", now);
      else screen_ = Screen::Standby;
    }
    return;
  }
  if (screen_ == Screen::WifiEdit) {
    if (input.bShort) {
      editor_.close(settings_);
      access_ = {};
      courseOffset_ = 0;
      screen_ = Screen::CoursePlan;
    }
    return;
  }
  if (screen_ == Screen::CoursePlan) {
    if (input.bShort) screen_ = Screen::Standby;
    else if (input.aShort) courseOffset_ = courseOffset_ + 3 < settings_.course.segments.size() ? courseOffset_ + 3 : 0;
    return;
  }
  if (screen_ != Screen::Standby) return;
  if (input.bShort) {
    // Retry the read on each request, allowing recovery from a transient I/O error.
    const auto loaded = store_.load(review_);
    haveReview_ = loaded == LoadResult::Found;
    if (haveReview_) {
      screen_ = Screen::Review;
      reviewPage_ = questionOffset_ = 0;
      return;
    }
    message(loaded == LoadResult::Empty ? "完了記録はありません" : "保存記録を読めません", now);
  }
  if (!input.aLong) return;
  LocalSeconds rtc = 0;
  const bool clockSet = clock_.read(rtc);
  if (!clockSet) { message("現在日時を設定してください", now); return; }
  if (!validCourse(settings_.course)) { message("設定エラー：コースを確認", now); return; }
  if (!deadlineConfirmed_) { message("終了日時をタッチして確定", now); return; }
  const auto error = tour_.start(settings_.course, rtc, clockSet, deadline_, now, settings_.warnings);
  if (error != StartError::None) { message("終了日時を未来に直してください", now); return; }
  screen_ = Screen::Running;
  message_.clear();
  deadlineBannerUntil_ = 0;
  haptics_.request(tour_.update(now).notice, now);
}
void App::openEditor(bool clock, Millis now) {
  LocalSeconds rtc = 0;
  const bool set = clock_.read(rtc);
  if (!clock && !set) { message("先に現在日時を設定してください", now); return; }
  edit_ = fromSeconds(clock ? (set ? rtc : toSeconds({2026,1,1,0,0,0})) : deadline_);
  edit_.second = 0;
  field_ = clock ? 0 : 4;
  screen_ = clock ? Screen::EditClock : Screen::EditDeadline;
  message_.clear();
}
void App::changeField(int direction) {
  int* fields[] = {&edit_.year, &edit_.month, &edit_.day, &edit_.hour, &edit_.minute};
  const int lo[] = {2000,1,1,0,0}, hi[] = {2099,12,31,23,59};
  auto& value = *fields[field_];
  value += direction;
  if (value < lo[field_]) value = hi[field_];
  if (value > hi[field_]) value = lo[field_];
  // For month/year changes, clamp an existing 29th/30th/31st to a valid day.
  if (field_ != 2) while (!validDate(edit_) && edit_.day > 1) --edit_.day;
  else {
    while (!validDate(edit_)) {
      edit_.day += direction;
      if (edit_.day < 1) edit_.day = 31;
      if (edit_.day > 31) edit_.day = 1;
    }
  }
}
void App::tap(int x, int y, Millis now) {
  releaseTouch(x, y, x, y, now);
}
void App::releaseTouch(int startX, int startY, int endX, int endY, Millis now) {
  const int dx = std::abs(endX - startX), dy = endY - startY;
  if (std::abs(dy) > 30 && std::abs(dy) > dx) { swipe(dy); return; }
  if (dx > 40 || std::abs(dy) > 30) return;
  const auto v = view(now);
  for (const auto& box : v.boxes) {
    if (box.action == Action::None || !contains(box, startX, startY)) continue;
    if (!contains(box, endX, endY)) return;
    switch (box.action) {
      case Action::Clock: openEditor(true, now); break;
      case Action::Deadline: openEditor(false, now); break;
      case Action::Start: update(now, {false,true,false}); break;
      case Action::Course: message_.clear(); courseOffset_ = 0; screen_ = Screen::CoursePlan; break;
      case Action::WifiEdit:
        message_.clear();
        if (editor_.open(settings_, access_)) screen_ = Screen::WifiEdit;
        else message("Wi-Fiを開始できません", now);
        break;
      case Action::Records:
      case Action::Question: update(now, {false,false,true}); break;
      case Action::NextSegment: update(now, {true,false,false}); break;
      case Action::NextPage:
        if (screen_ == Screen::CoursePlan)
          courseOffset_ = courseOffset_ + 3 < settings_.course.segments.size() ? courseOffset_ + 3 : 0;
        else update(now, {true,false,false});
        break;
      case Action::Back:
        if (screen_ == Screen::Review || screen_ == Screen::WifiEdit) update(now, {false,false,true});
        else { screen_ = Screen::Standby; message_.clear(); }
        break;
      case Action::Retry: saveResult(now); break;
      case Action::PreviousValue: changeField(-1); break;
      case Action::NextValue: changeField(1); break;
      case Action::Year: case Action::Month: case Action::Day: case Action::Hour: case Action::Minute:
        field_ = static_cast<int>(box.action) - static_cast<int>(Action::Year);
        break;
      case Action::Save: confirmDate(now); break;
      case Action::None: break;
    }
    return;
  }
}
void App::confirmDate(Millis now) {
  if (!validDate(edit_)) { message("日時が正しくありません", now); return; }
  if (screen_ == Screen::EditClock) {
    if (!clock_.set(edit_)) { message("時計を設定できません", now); return; }
    deadlineConfirmed_ = false;
    deadline_ = suggestedDeadline(toSeconds(edit_));
  } else {
    LocalSeconds rtc = 0;
    if (!clock_.read(rtc) || toSeconds(edit_) <= rtc) {
      message("終了日時を未来に直してください", now); return;
    }
    deadline_ = toSeconds(edit_);
    deadlineConfirmed_ = true;
  }
  screen_ = Screen::Standby;
  message_.clear();
}
void App::swipe(int dy) {
  if (screen_ == Screen::CoursePlan) {
    if (dy < -30 && courseOffset_ + 3 < settings_.course.segments.size()) courseOffset_ += 3;
    if (dy > 30 && courseOffset_ >= 3) courseOffset_ -= 3;
    return;
  }
  if (screen_ != Screen::Review || reviewPage_ == 0) return;
  size_t count = 0;
  for (const auto& q : review_.questions) if (q.segment == reviewPage_ - 1) ++count;
  if (dy < -30 && questionOffset_ + 3 < count) questionOffset_ += 3;
  if (dy > 30) questionOffset_ = questionOffset_ >= 3 ? questionOffset_ - 3 : 0;
}
bool App::needsTimeRefresh(Millis lastFrame, Millis now) const {
  if (!message_.empty() && lastFrame < messageUntil_ && messageUntil_ <= now) return true;
  if (screen_ == Screen::Running) return now - lastFrame >= 100;
  return screen_ == Screen::Standby && now - lastFrame >= 1000;
}
View App::view(Millis now) {
  View v;
  const bool showMessage = !message_.empty() && now < messageUntil_;
  if (screen_ == Screen::EditClock || screen_ == Screen::EditDeadline) {
    const bool clock = screen_ == Screen::EditClock;
    const auto date = dateText(toSeconds(edit_));
    row(v, clock ? "時計を合わせる" : "終了時刻を設定", 49, 24);
    row(v, date.substr(0, 10), 88, 20, Ink::Muted);
    row(v, date.substr(11), 141, 56);
    static const char* units[] = {"年", "月", "日", "時", "分"};
    const int values[] = {edit_.year, edit_.month, edit_.day, edit_.hour, edit_.minute};
    const Action fields[] = {Action::Year, Action::Month, Action::Day, Action::Hour, Action::Minute};
    for (int i = 0; i < 5; ++i)
      button(v, units[i], fields[i], 57 + i * 71, 188, 68, 50,
             i == field_ ? Ink::Primary : Ink::Surface);
    button(v, "−", Action::PreviousValue, 82, 253, 72, 58);
    row(v, std::to_string(values[field_]) + units[field_], 282, 32, Ink::Normal, 142);
    button(v, "＋", Action::NextValue, 312, 253, 72, 58);
    button(v, "戻る", Action::Back, 76, 336, 144, 66);
    button(v, "確定", Action::Save, 231, 336, 159, 66, Ink::Primary);
    if (showMessage) row(v, message_, 322, 20, Ink::Warning, 335);
    return v;
  }
  if (screen_ == Screen::WifiEdit) {
    row(v, "Wi-Fiで設定", 60, 32);
    row(v, "接続先", 115, 20, Ink::Muted);
    row(v, access_.ssid, 146, 32);
    row(v, "パスワード", 196, 20, Ink::Muted);
    row(v, access_.password, 227, 32);
    row(v, "接続後、ブラウザーで開く", 278, 20, Ink::Muted);
    row(v, access_.url, 309, 24, Ink::Accent);
    button(v, "Wi-Fiを終了", Action::Back, 120, 350, 226, 64);
    return v;
  }
  if (screen_ == Screen::Standby || screen_ == Screen::CoursePlan) {
    int64_t standard = 0;
    for (const auto& s : settings_.course.segments) standard += int64_t(s.seconds) * 1000;
    if (screen_ == Screen::CoursePlan) {
      row(v, "コース配分", 48, 32);
      row(v, showMessage ? message_ : settings_.course.name + "  ·  標準 " + duration(standard),
          89, 20, showMessage ? Ink::Warning : Ink::Muted);
      for (size_t i = courseOffset_; i < std::min(courseOffset_ + 3, settings_.course.segments.size()); ++i) {
        const auto& segment = settings_.course.segments[i];
        const int y = 114 + int(i - courseOffset_) * 58;
        surface(v, 59, y, 348, 54, Ink::Surface, Action::None, 16);
        row(v, segment.name, y + 27, 24, Ink::Normal, 203, 172, Ink::Surface);
        row(v, duration(int64_t(segment.seconds) * 1000), y + 27, 24, Ink::Accent, 111, 339, Ink::Surface);
      }
      button(v, "Wi-Fiで設定", Action::WifiEdit, 89, 291, 179, 49, Ink::Surface, 20);
      row(v, std::to_string(courseOffset_ / 3 + 1) + " / " +
             std::to_string(std::max<size_t>(1, (settings_.course.segments.size() + 2) / 3)), 315, 20, Ink::Muted, 82, 330);
      button(v, "戻る", Action::Back, settings_.course.segments.size() > 3 ? 97 : 168, 350, 130, 64);
      if (settings_.course.segments.size() > 3) button(v, "次へ", Action::NextPage, 237, 350, 132, 64);
      return v;
    }
    LocalSeconds rtc = 0;
    const bool clockSet = clock_.read(rtc);
    const bool ready = clockSet && deadlineConfirmed_ && deadline_ > rtc;
    const auto end = dateText(deadline_);
    row(v, settings_.course.name, 52, 32, Ink::Normal, 248);
    button(v, clockSet ? "現在 " + dateText(rtc) + "  ›" : "現在日時を設定  ›",
           Action::Clock, 67, 84, 332, 56, Ink::Surface, 20);
    surface(v, 55, 151, 356, 108, Ink::Surface,
            clockSet ? Action::Deadline : Action::Clock, 28);
    const bool invalid = !validCourse(settings_.course);
    const std::string caption = invalid ? "コース設定を確認してください" :
        showMessage ? message_ : clockSet ? "終了予定   " + end.substr(0,10) : "終了予定";
    row(v, caption, 176, 20, invalid || showMessage ? Ink::Warning : Ink::Muted,
        331, 233, Ink::Surface);
    row(v, clockSet ? end.substr(11) : "--:--", 220, 56, Ink::Normal, 256, 233, Ink::Surface);
    button(v, !clockSet ? "時計を合わせる" : ready ? "案内を開始" : "終了時刻を設定",
           !clockSet ? Action::Clock : ready ? Action::Start : Action::Deadline,
           69, 271, 328, 62, Ink::Primary);
    button(v, "コース配分", Action::Course, 76, 343, 151, 64, Ink::Surface, 20);
    button(v, "保存記録", Action::Records, 239, 343, 151, 64, Ink::Surface, 20);
    if (ready && !invalid) row(v, "A 長押しでも開始", 419, 20, Ink::Muted, 236);
    return v;
  }
  if (screen_ == Screen::Running) {
    const auto left = tour_.remainingMs(now), segmentLeft = tour_.segmentRemainingMs(now);
    const auto& r = tour_.record();
    const int64_t availableMs = (r.deadline - r.started) * 1000;
    v.remainingPercent = static_cast<int>(100 * std::max<int64_t>(0, left) / availableMs);
    v.ringInk = left <= int64_t(settings_.warnings.overall) * 1000 ? Ink::Warning : Ink::Accent;
    v.segmentIndex = tour_.segment();
    v.segmentCount = r.segments.size();
    const auto& segment = r.course.segments[v.segmentIndex];
    const auto budget = tour_.segmentBudgetMs();
    v.segmentRemainingFraction = segmentLeft > 0 && budget > 0
        ? static_cast<float>(segmentLeft) / budget : 0.0f;
    row(v, "終了 " + dateText(r.deadline), 70, 20, Ink::Muted, 270);
    row(v, now < deadlineBannerUntil_ && left > -1000 ? "終了時刻" : left < 0 ? "終了超過" : "終了まで",
        108, 24, v.ringInk);
    row(v, duration(left > 0 ? left + 999 : left), 172, 76,
        v.ringInk == Ink::Warning ? Ink::Warning : Ink::Normal);
    row(v, segment.name, 244, 32, v.segmentCount > 1 ? Ink::Segment : Ink::Normal);
    row(v, std::string(segmentLeft < 0 ? "目安超過  " : "今回の目安  ") +
        duration(segmentLeft > 0 ? segmentLeft + 999 : segmentLeft), 281, 24, Ink::Muted);
    size_t count = 0;
    for (const auto& q : r.questions) if (q.segment == v.segmentIndex) ++count;
    row(v, showMessage ? message_ : "区間 " + countText(v.segmentIndex+1) + " / " + countText(v.segmentCount) +
        "    質問 " + countText(count) + "件", 313, 20, Ink::Muted);
    if (v.segmentIndex + 1 < v.segmentCount) {
      button(v, "A  次の区間", Action::NextSegment, 85, 330, 142, 54, Ink::Surface, 20);
      button(v, "B  質問を記録", Action::Question, 239, 330, 142, 54, Ink::Surface, 20);
    } else button(v, "B  質問を記録", Action::Question, 109, 330, 248, 54);
    row(v, "Aを0.8秒長押しで終了", 396, 20, Ink::Muted, 244);
    return v;
  }
  if (!haveReview_) return v;
  const size_t index = reviewPage_ ? reviewPage_ - 1 : 0;
  row(v, reviewPage_ ? review_.course.segments[index].name : "全体の振り返り", 54, 32, Ink::Normal, 267);
  if (reviewPage_ == 0) {
    row(v, review_.course.name, 93, 20, Ink::Muted);
    row(v, "全体所要", 131, 20, Ink::Accent);
    row(v, duration(review_.elapsed), 178, 56);
    const auto diff = (review_.ended() - review_.deadline) * 1000;
    row(v, diff == 0 ? "定刻に終了" : (diff < 0 ? "予定より早い " : "予定より遅い ") + duration(diff),
        229, 24, diff > 0 ? Ink::Warning : Ink::Accent);
    row(v, "予定  " + dateText(review_.deadline,true), 271, 20, Ink::Muted);
    row(v, "実際  " + dateText(review_.ended(),true), 299, 20);
    row(v, "質問 " + countText(review_.questions.size()) + "件", 330, 20, Ink::Muted);
  } else {
    const auto& s = review_.segments[index];
    const int64_t standard = int64_t(review_.course.segments[index].seconds)*1000;
    row(v, "区間 " + countText(reviewPage_) + " / " + countText(review_.segments.size()) +
           "    標準 " + duration(standard), 102, 20, Ink::Muted);
    row(v, s.visited ? "実績 " + duration(s.end-s.start) + " / 差 " +
        difference(int64_t((s.end-s.start)/1000)*1000-standard) : "未実施", 152, 24);
    std::vector<Question> questions;
    for (const auto& q : review_.questions) if (q.segment == index) questions.push_back(q);
    row(v, "質問 " + countText(questions.size()) + "件", 200, 24, Ink::Accent);
    for (size_t i=questionOffset_; i<std::min(questionOffset_+3,questions.size()); ++i) {
      const auto& q=questions[i];
      row(v, countText(i+1)+".  "+dateText(review_.started+q.elapsed/1000,true).substr(11) +
             "   開始から " + duration(q.elapsed), 240+int(i-questionOffset_)*32, 20);
    }
    if (questions.empty()) row(v, "質問の記録なし", 257, 24, Ink::Muted);
    if (questions.size() > 3) row(v, countText(questionOffset_+1)+"–"+
        countText(std::min(questionOffset_+3,questions.size()))+"件目  ·  上下にスワイプ", 333, 20, Ink::Muted);
  }
  if (unsaved_) button(v, "再保存", Action::Retry, 80, 347, 147, 64, Ink::Primary, 20);
  else button(v, "待機に戻る", Action::Back, 80, 347, 147, 64, Ink::Surface, 20);
  button(v, reviewPage_ == review_.segments.size() ? "全体へ" : "次の区間", Action::NextPage,
         239, 347, 147, 64, Ink::Surface, 20);
  if (unsaved_ || showMessage) row(v, showMessage ? message_ : "記録が未保存です", 423, 20,
                                   unsaved_ ? Ink::Warning : Ink::Muted, 224);
  return v;
}
}  // namespace tour
