#pragma once

#include "esp_err.h"
#include "power_actuator.hpp"
#include "wifi_manager.hpp"
#include "environment_sensor.hpp"
#include "power_led_monitor.hpp"
#include "reachability_monitor.hpp"

namespace pc_power {

class CloudClient {
 public:
  esp_err_t start(WifiManager* wifi, PowerActuator* actuator,
                  ReachabilityMonitor* reachability, EnvironmentSensor* environment,
                  PowerLedMonitor* power_led);
  bool is_configured() const { return configured_; }
  bool is_online() const { return online_; }
  static esp_err_t configure(const char* url, const char* device_id, const char* credential);
  static esp_err_t pair(const char* url, const char* device_id, const char* pairing_code);

 private:
  static void task_entry(void* arg);
  void task_loop();
  bool load_config();
  bool poll_once();
  volatile bool reload_requested_ = false;
  volatile bool online_ = false;
  bool configured_ = false;

  WifiManager* wifi_ = nullptr;
  PowerActuator* actuator_ = nullptr;
  ReachabilityMonitor* reachability_ = nullptr;
  EnvironmentSensor* environment_ = nullptr;
  PowerLedMonitor* power_led_ = nullptr;
  char base_url_[192]{};
  char device_id_[65]{};
  char credential_[129]{};
};

}  // namespace pc_power
