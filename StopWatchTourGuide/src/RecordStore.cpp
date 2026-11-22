#include "RecordStore.h"
#include <limits>

namespace tour {
namespace {
constexpr uint32_t kMagic = 0x52554754; // TGUR, little endian.
uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < n; ++i) {
    crc ^= p[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
struct Writer {
  std::vector<uint8_t> data;
  void number(uint64_t n, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) { data.push_back(n & 255); n >>= 8; }
  }
  void string(const std::string& s) {
    number(s.size(), 2);
    data.insert(data.end(), s.begin(), s.end());
  }
};
struct Reader {
  const std::vector<uint8_t>& data;
  size_t position = 0;
  bool ok = true;
  uint64_t number(unsigned bytes) {
    if (position + bytes > data.size()) { ok = false; return 0; }
    uint64_t n = 0;
    for (unsigned i = 0; i < bytes; ++i) n |= uint64_t(data[position++]) << (8 * i);
    return n;
  }
  std::string string() {
    const auto n = number(2);
    if (!ok || n > 96 || position + n > data.size()) { ok = false; return {}; }
    std::string s(data.begin() + position, data.begin() + position + n);
    position += n;
    if (s.find('\0') != std::string::npos) ok = false;
    return s;
  }
};
struct Slot {
  ReadResult read = ReadResult::Missing;
  bool valid = false;
  uint64_t generation = 0;
  Record record;
  std::vector<uint8_t> bytes;
};
Slot readSlot(BlobStore& blobs, unsigned slot) {
  Slot s;
  s.read = blobs.read(slot, s.bytes);
  if (s.read == ReadResult::Ok) s.valid = decodeRecord(s.bytes, s.record, s.generation);
  return s;
}
int latest(const Slot& a, const Slot& b) {
  if (!a.valid && !b.valid) return -1;
  if (!a.valid) return 1;
  if (!b.valid) return 0;
  return b.generation > a.generation ? 1 : 0;
}
}  // namespace

std::vector<uint8_t> encodeRecord(const Record& r, uint64_t generation) {
  if (!validRecord(r) || generation == 0) return {};
  Writer w;
  w.number(kMagic, 4);
  w.number(r.version, 4);
  w.number(generation, 8);
  if (r.version == kDurationRecordVersion) {
    w.number(r.durationSeconds, 4);
  } else {
    w.number(r.deadline, 8);
    w.number(r.started, 8);
  }
  w.number(r.elapsed, 8);
  w.string(r.course.name);
  w.number(r.segments.size(), 2);
  for (size_t i = 0; i < r.segments.size(); ++i) {
    w.string(r.course.segments[i].name);
    w.number(r.course.segments[i].seconds, 4);
    w.number(r.segments[i].visited, 1);
    w.number(r.segments[i].start, 8);
    w.number(r.segments[i].end, 8);
  }
  w.number(r.questions.size(), 4);
  for (const auto& q : r.questions) {
    w.number(q.segment, 4);
    w.number(q.elapsed, 8);
  }
  w.number(crc32(w.data.data(), w.data.size()), 4);
  if (w.data.size() > kMaxRecordBytes) return {};
  return w.data;
}
bool decodeRecord(const std::vector<uint8_t>& bytes, Record& result, uint64_t& generation) {
  if (bytes.size() < 48 || bytes.size() > kMaxRecordBytes) return false;
  Reader checksum{bytes};
  checksum.position = bytes.size() - 4;
  if (checksum.number(4) != crc32(bytes.data(), bytes.size() - 4)) return false;
  Reader r{bytes};
  if (r.number(4) != kMagic) return false;
  Record value;
  value.version = r.number(4);
  const auto gen = r.number(8);
  if (value.version == kDurationRecordVersion) {
    const auto seconds = r.number(4);
    if (!seconds || seconds > INT32_MAX) return false;
    value.durationSeconds = static_cast<int32_t>(seconds);
  } else if (value.version == kRecordVersion) {
    const auto deadline = r.number(8), started = r.number(8);
    if (deadline > uint64_t(INT64_MAX) || started > uint64_t(INT64_MAX)) return false;
    value.deadline = static_cast<int64_t>(deadline);
    value.started = static_cast<int64_t>(started);
  } else return false;
  value.elapsed = r.number(8);
  value.course.name = r.string();
  const auto count = r.number(2);
  if (!r.ok || count == 0 || count > kMaxSegments) return false;
  for (size_t i = 0; i < count; ++i) {
    auto name = r.string();
    const auto seconds = r.number(4), visited = r.number(1);
    if (seconds > INT32_MAX || visited > 1) return false;
    value.course.segments.push_back({name, static_cast<int32_t>(seconds)});
    const auto start = r.number(8), end = r.number(8);
    value.segments.push_back({visited != 0, start, end});
  }
  const auto questions = r.number(4);
  if (!r.ok || questions > kMaxQuestions) return false;
  for (size_t i = 0; i < questions; ++i) {
    const auto segment = r.number(4), elapsed = r.number(8);
    value.questions.push_back({static_cast<uint32_t>(segment), elapsed});
  }
  if (!r.ok || r.position != bytes.size() - 4 || gen == 0 || !validRecord(value)) return false;
  result = value;
  generation = gen;
  return true;
}
LoadResult RecordStore::load(Record& result) {
  const auto a = readSlot(blobs_, 0), b = readSlot(blobs_, 1);
  // Do not expose an older record as the latest when a slot could not be read.
  if (a.read == ReadResult::Error || b.read == ReadResult::Error) return LoadResult::IoError;
  const int selected = latest(a, b);
  if (selected >= 0) { result = selected == 0 ? a.record : b.record; return LoadResult::Found; }
  return a.read == ReadResult::Missing && b.read == ReadResult::Missing ?
      LoadResult::Empty : LoadResult::Damaged;
}
bool RecordStore::save(const Record& record) {
  const auto a = readSlot(blobs_, 0), b = readSlot(blobs_, 1);
  if (a.read == ReadResult::Error || b.read == ReadResult::Error) return false;
  const int active = latest(a, b);
  const auto generation = active == 0 ? a.generation : active == 1 ? b.generation : 0;
  if (generation == std::numeric_limits<uint64_t>::max()) return false;
  const auto bytes = encodeRecord(record, generation + 1);
  if (bytes.empty()) return false;
  const unsigned target = active == 0 ? 1 : 0;
  if (!blobs_.write(target, bytes)) return false;
  const auto check = readSlot(blobs_, target);
  // The other slot is never deleted. Boot selects the newest CRC-valid record.
  return check.valid && check.generation == generation + 1 && check.bytes == bytes;
}
}  // namespace tour
