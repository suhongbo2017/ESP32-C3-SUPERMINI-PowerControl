#include "local_auth.hpp"
#include "local_server.hpp"
#include "maintenance_console.hpp"
#include "power_actuator.hpp"
#include "reachability_monitor.hpp"
#include "wifi_manager.hpp"
#include "environment_sensor.hpp"
#include "power_led_monitor.hpp"
#include "control_policy.hpp"
#include "cloud_client.hpp"

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "pc-power";
pc_power::PowerActuator g_power_actuator;
pc_power::LocalAuth g_local_auth;
pc_power::LocalServer g_local_server;
pc_power::MaintenanceConsole g_maintenance_console;
pc_power::WifiManager g_wifi_manager;
pc_power::ReachabilityMonitor g_reachability_monitor;
pc_power::EnvironmentSensor g_environment_sensor;
pc_power::PowerLedMonitor g_power_led_monitor;
pc_power::CloudClient g_cloud_client;
}  // namespace

extern "C" void app_main() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  ESP_ERROR_CHECK(g_local_auth.begin());
  ESP_ERROR_CHECK(g_power_actuator.start());
  ESP_ERROR_CHECK(g_environment_sensor.start());
  ESP_ERROR_CHECK(g_power_led_monitor.start());
  err = g_maintenance_console.start(&g_local_auth, &g_wifi_manager,
                                    &g_reachability_monitor);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "maintenance commands unavailable: %s", esp_err_to_name(err));
  }
  ESP_ERROR_CHECK(g_wifi_manager.start());
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, const_cast<char*>("pool.ntp.org"));
  esp_sntp_init();
  ESP_ERROR_CHECK(g_reachability_monitor.start(&g_wifi_manager));
  ESP_ERROR_CHECK(g_cloud_client.start(&g_wifi_manager, &g_power_actuator,
                                       &g_reachability_monitor,
                                       &g_environment_sensor,
                                       &g_power_led_monitor));
  ESP_LOGI(kTag, "startup complete; credentials_configured=%s",
           g_local_auth.is_configured() ? "true" : "false");
  while (true) {
    const bool station_available = g_wifi_manager.is_station_connected();
    if (station_available && !g_local_server.is_started()) {
      ESP_ERROR_CHECK(g_local_server.start(&g_local_auth, &g_power_actuator,
                                           &g_reachability_monitor,
                                           &g_wifi_manager,
                                           &g_environment_sensor,
                                           &g_power_led_monitor,
                                           &g_cloud_client));
    } else if (!station_available && g_local_server.is_started()) {
      g_local_server.stop();
      ESP_LOGW(kTag, "local HTTP control stopped; station/auth unavailable");
    }

    if (g_wifi_manager.is_setup_ap_active() &&
        !g_local_server.is_setup_started()) {
      const esp_err_t setup_err = g_local_server.start_setup(&g_wifi_manager);
      if (setup_err != ESP_OK) {
        ESP_LOGE(kTag, "setup HTTP server failed: %s",
                 esp_err_to_name(setup_err));
      }
    } else if (!g_wifi_manager.is_setup_ap_active() &&
               g_local_server.is_setup_started()) {
      g_local_server.stop_setup();
      ESP_LOGI(kTag, "setup HTTP server stopped");
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
