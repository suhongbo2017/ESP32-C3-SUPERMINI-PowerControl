#pragma once

#include <atomic>

#include "esp_err.h"

namespace pc_power {

enum class PowerLedState { kLow, kHigh, kBlinking };

class PowerLedMonitor {
 public:
  esp_err_t start();
  bool level() const { return level_.load(); }
  PowerLedState state() const { return state_.load(); }

 private:
  static void task_entry(void* arg);
  void task_loop();
  std::atomic<bool> level_{false};
  std::atomic<PowerLedState> state_{PowerLedState::kLow};
};

}  // namespace pc_power
