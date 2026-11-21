#pragma once
#include "CourseEditor.h"
#include <esp_http_server.h>

namespace tour {
class WifiCourseEditor final : public CourseEditor {
 public:
  LoadResult load(GuideSettings&) override;
  bool open(const GuideSettings&, WifiAccess&) override;
  void close(GuideSettings&) override;
 private:
  httpd_handle_t server_ = nullptr;
  // Owned by the HTTP server task from startup until httpd_stop() returns.
  GuideSettings saved_;
  static esp_err_t handle(httpd_req_t* request);
  void stop();
};
}  // namespace tour
