#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace pc_power {

enum class PressResult : uint8_t {
  kAccepted,
  kDuplicate,
  kBusy,
  kInvalidCommand,
};

class PowerActuator {
 public:
  static constexpr int kGpio = 1;
  static constexpr uint32_t kPulseMs = 300;
  static constexpr size_t kRememberedCommandCount = 8;

  esp_err_t start();
  PressResult request_press(const char* command_id);

 private:
  static void worker_entry(void* context);
  void worker_loop();
  bool has_seen(const char* command_id) const;
  void remember(const char* command_id);

  void* mutex_ = nullptr;
  void* queue_ = nullptr;
  bool busy_ = false;
  size_t remembered_count_ = 0;
  char remembered_ids_[kRememberedCommandCount][37]{};
};

}  // namespace pc_power
