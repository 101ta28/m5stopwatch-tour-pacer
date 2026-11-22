#pragma once
#include "App.h"

namespace tour {
// Newline-delimited USB commands. No waiting for a host or a complete line.
class UsbCommands {
 public:
  std::string receive(char byte, App& app);
 private:
  std::string courseCommand(std::string_view command, App& app);
  char line_[128];  // Prefix, 10-digit duration, tab, 96-byte name and optional CR.
  size_t length_ = 0;
  bool receivingCourse_ = false;
  std::string courseText_;
};
}  // namespace tour
