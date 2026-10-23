#pragma once

#include <atomic>

#include "esp_err.h"

namespace pc_power {

class PowerLedMonitor {
 public:
  esp_err_t start();
  bool level() const { return level_.load(); }

 private:
  static void task_entry(void* arg);
  void task_loop();
  std::atomic<bool> level_{false};
};

}  // namespace pc_power
