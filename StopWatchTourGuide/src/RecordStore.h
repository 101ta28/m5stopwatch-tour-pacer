#pragma once
#include "Tour.h"

namespace tour {
// Storage returns Missing only for a genuinely absent key; I/O errors must not
// be mistaken for an empty slot, which could overwrite the last good record.
enum class ReadResult { Ok, Missing, Error };
class BlobStore {
 public:
  virtual ~BlobStore() = default;
  virtual ReadResult read(unsigned slot, std::vector<uint8_t>& data) = 0;
  virtual bool write(unsigned slot, const std::vector<uint8_t>& data) = 0;
};
constexpr size_t kMaxRecordBytes = 24576;
std::vector<uint8_t> encodeRecord(const Record&, uint64_t generation);
bool decodeRecord(const std::vector<uint8_t>&, Record&, uint64_t& generation);
enum class LoadResult { Found, Empty, Damaged, IoError };
class RecordStore {
 public:
  explicit RecordStore(BlobStore& blobs) : blobs_(blobs) {}
  LoadResult load(Record& result);
  bool save(const Record& record);
 private:
  BlobStore& blobs_;
};
}  // namespace tour
