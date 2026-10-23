#pragma once

#include <atomic>

#include "esp_err.h"
#include "esp_event.h"

namespace pc_power {

class WifiManager {
 public:
  esp_err_t start();
  esp_err_t configure(const char* ssid, const char* password);
  bool is_ready() const { return ready_.load(); }
  bool is_station_connected() const { return station_connected_.load(); }
  bool is_setup_ap_active() const { return ap_started_.load(); }
  esp_err_t get_station_ssid(char* output, size_t output_size) const;

 private:
  static void event_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data);
  esp_err_t connect_saved_or_start_ap();
  esp_err_t start_setup_ap();

  bool initialized_ = false;
  std::atomic<bool> ready_{false};
  std::atomic<bool> station_connected_{false};
  std::atomic<bool> ap_started_{false};
  std::atomic<int> retry_count_{0};
};

}  // namespace pc_power
