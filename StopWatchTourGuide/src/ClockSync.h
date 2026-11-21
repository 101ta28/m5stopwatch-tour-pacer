#pragma once
#include "App.h"

namespace tour {
// Newline-delimited USB commands. No waiting for a host or a complete line.
class ClockSync {
 public:
  std::string receive(char byte, App& app);
 private:
  char line_[40];
  size_t length_ = 0;
};
}  // namespace tour
