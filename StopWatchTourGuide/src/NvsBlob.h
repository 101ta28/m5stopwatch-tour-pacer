#pragma once
#include "RecordStore.h"

namespace tour {
ReadResult readNvsBlob(const char* key, size_t limit, std::vector<uint8_t>& data);
bool writeNvsBlob(const char* key, const void* data, size_t size);
}  // namespace tour
