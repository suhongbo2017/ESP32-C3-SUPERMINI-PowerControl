#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#include "cloud_client.hpp"
#include "local_auth.hpp"
#include "power_actuator.hpp"
#include "reachability_monitor.hpp"
#include "wifi_manager.hpp"
#include "environment_sensor.hpp"
#include "power_led_monitor.hpp"

namespace pc_power {

class LocalServer {
 public:
  esp_err_t start(LocalAuth* auth, PowerActuator* actuator,
                  ReachabilityMonitor* reachability, WifiManager* wifi,
                  EnvironmentSensor* environment, PowerLedMonitor* power_led,
                  CloudClient* cloud);
  esp_err_t start_setup(WifiManager* wifi);
  bool is_started() const { return server_ != nullptr; }
  bool is_setup_started() const { return setup_server_ != nullptr; }
  void stop();
  void stop_setup();

 private:
  httpd_handle_t server_ = nullptr;
  httpd_handle_t setup_server_ = nullptr;
  void* context_ = nullptr;
  void* setup_context_ = nullptr;
  static esp_err_t setup_page_handler(httpd_req_t* request);
  static esp_err_t setup_wifi_handler(httpd_req_t* request);
  static esp_err_t setup_send(httpd_req_t* request, const char* body,
                              const char* status = "200 OK");
  static esp_err_t page_handler(httpd_req_t* request);
  static esp_err_t settings_page_handler(httpd_req_t* request);
  static esp_err_t local_config_handler(httpd_req_t* request);
  static esp_err_t password_handler(httpd_req_t* request);
  static esp_err_t wifi_handler(httpd_req_t* request);
  static esp_err_t cloud_config_handler(httpd_req_t* request);
  static esp_err_t ping_handler(httpd_req_t* request);
  static esp_err_t status_handler(httpd_req_t* request);
  static esp_err_t power_press_handler(httpd_req_t* request);
  static esp_err_t unauthorized(httpd_req_t* request);
  static bool authorize(httpd_req_t* request, LocalAuth* auth);
  static void make_command_id(char output[37]);
};

}  // namespace pc_power
