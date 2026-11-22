#include "WifiCourseEditor.h"
#include "CoursePage.h"
#include "NvsBlob.h"
#include <WiFi.h>
#include <algorithm>
#include <utility>
#include <esp_mac.h>
#include <esp_random.h>

namespace tour {
namespace {
constexpr const char* kCourseKey = "course-v1";

esp_err_t reply(httpd_req_t* req, const char* status, const char* type, std::string_view body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body.data(), body.size());
}
bool devicePassword(std::string& password) {
  constexpr const char* key = "wifi-pass";
  std::vector<uint8_t> bytes;
  const auto read = readNvsBlob(key, 8, bytes);
  if (read == ReadResult::Error) return false;
  if (bytes.size() == 8 && std::all_of(bytes.begin(), bytes.end(),
                                       [](uint8_t b) { return b >= '0' && b <= '9'; })) {
    password.assign(bytes.begin(), bytes.end());
    return true;
  }
  char digits[9];
  snprintf(digits, sizeof(digits), "%08lu", static_cast<unsigned long>(esp_random() % 100000000));
  password = digits;
  return writeNvsBlob(key, password.data(), password.size());
}

constexpr const char* kText = "text/plain; charset=utf-8";
}  // namespace

LoadResult WifiCourseEditor::load(GuideSettings& settings) {
  std::vector<uint8_t> bytes;
  const auto result = readNvsBlob(kCourseKey, kMaxCourseBytes, bytes);
  if (result == ReadResult::Missing) return LoadResult::Empty;
  if (result == ReadResult::Error) return LoadResult::IoError;
  if (bytes.empty() || !parseSettings({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, settings))
    return LoadResult::Damaged;
  return LoadResult::Found;
}

bool WifiCourseEditor::writeCourse(std::string_view text) {
  return writeNvsBlob(kCourseKey, text.data(), text.size());
}

bool WifiCourseEditor::open(const GuideSettings& settings, WifiAccess& access) {
  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) return false;
  std::string password;
  if (!devicePassword(password)) return false;
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "M5StopWatch-%02X%02X", mac[4], mac[5]);
  WiFi.persistent(false);
  if (!WiFi.softAP(ssid, password.c_str(), 1, 0, 1)) { stop(); return false; }

  saved_ = settings;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.max_req_hdr_len = 2048;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 2;
  if (httpd_start(&server_, &config) != ESP_OK) { stop(); return false; }

  httpd_uri_t route{};
  route.uri = "/";
  route.method = HTTP_GET;
  route.handler = [](httpd_req_t* req) { return reply(req, "200 OK", "text/html; charset=utf-8", kCoursePage); };
  const auto page = httpd_register_uri_handler(server_, &route);
  route.handler = handle;
  route.user_ctx = this;
  route.uri = "/course";
  const auto get = httpd_register_uri_handler(server_, &route);
  route.method = HTTP_PUT;
  if (page != ESP_OK || get != ESP_OK || httpd_register_uri_handler(server_, &route) != ESP_OK) {
    stop();
    return false;
  }
  access = {ssid, password, std::string("http://") + WiFi.softAPIP().toString().c_str()};
  return true;
}

esp_err_t WifiCourseEditor::handle(httpd_req_t* req) {
  auto& editor = *static_cast<WifiCourseEditor*>(req->user_ctx);
  if (req->method == HTTP_GET) return reply(req, "200 OK", kText, settingsText(editor.saved_));

  if (req->content_len == 0 || req->content_len > kMaxCourseBytes) {
    reply(req, "413 Payload Too Large", kText, "設定データのサイズを確認してください。");
    return ESP_FAIL; // Close the connection with its unread request body.
  }
  std::string body(req->content_len, '\0');
  size_t received = 0;
  const auto started = millis();
  while (received < body.size()) {
    const int count = httpd_req_recv(req, &body[received], body.size() - received);
    if (count <= 0 || millis() - started > 5000) return ESP_FAIL;
    received += count;
  }
  const auto saved = editor.save(body, editor.saved_);
  if (saved == CourseSave::Invalid)
    return reply(req, "400 Bad Request", kText, "名前、区間数、目安と予告の秒数を確認してください。");
  if (saved == CourseSave::Failed)
    return reply(req, "500 Internal Server Error", kText, "本体に保存できませんでした。もう一度保存してください。");
  return reply(req, "200 OK", kText, "本体に保存しました。StopWatchの「Wi-Fiを終了」を押してください。");
}

void WifiCourseEditor::stop() {
  if (server_) {
    httpd_stop(server_);
    server_ = nullptr;
  }
  WiFi.mode(WIFI_OFF);
}
void WifiCourseEditor::close(GuideSettings& settings) {
  stop();
  settings = std::move(saved_);
}
}  // namespace tour
