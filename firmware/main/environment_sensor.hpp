#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "esp_err.h"
#include "driver/i2c_master.h"

namespace pc_power {

class EnvironmentSensor {
 public:
  esp_err_t start();
  bool available() const { return available_.load(); }
  bool read(float* temperature_c, float* humidity_percent) const;

 private:
  static void task_entry(void* arg);
  void task_loop();
  bool read_once(float* temperature_c, float* humidity_percent) const;

  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  mutable std::mutex lock_;
  float temperature_c_ = 0.0f;
  float humidity_percent_ = 0.0f;
  std::atomic<bool> available_{false};
};

}  // namespace pc_power
