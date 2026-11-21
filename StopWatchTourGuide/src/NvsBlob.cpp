#include "NvsBlob.h"
#include <nvs.h>

namespace tour {
ReadResult readNvsBlob(const char* key, size_t limit, std::vector<uint8_t>& data) {
  nvs_handle_t handle;
  const auto opened = nvs_open("tour-guide", NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return ReadResult::Missing;
  if (opened != ESP_OK) return ReadResult::Error;
  size_t size = 0;
  const auto found = nvs_get_blob(handle, key, nullptr, &size);
  if (found != ESP_OK) {
    nvs_close(handle);
    return found == ESP_ERR_NVS_NOT_FOUND ? ReadResult::Missing : ReadResult::Error;
  }
  if (size > limit) { data.clear(); nvs_close(handle); return ReadResult::Ok; }
  data.resize(size);
  const auto read = nvs_get_blob(handle, key, data.data(), &size);
  nvs_close(handle);
  return read == ESP_OK ? ReadResult::Ok : ReadResult::Error;
}
bool writeNvsBlob(const char* key, const void* data, size_t size) {
  nvs_handle_t handle;
  if (nvs_open("tour-guide", NVS_READWRITE, &handle) != ESP_OK) return false;
  const auto written = nvs_set_blob(handle, key, data, size);
  const bool ok = written == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}
}  // namespace tour
