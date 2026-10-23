#include "cloud_client.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "cloud-client";
constexpr char kNamespace[] = "cloud_config";
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(5000);

bool read_string(nvs_handle_t handle, const char* key, char* output, size_t size) {
  size_t length = size;
  return nvs_get_str(handle, key, output, &length) == ESP_OK;
}
bool now_iso(char output[32]) {
  const time_t now = time(nullptr);
  if (now < 1704067200) return false;
  struct tm utc{};
  gmtime_r(&now, &utc);
  return strftime(output, 32, "%Y-%m-%dT%H:%M:%S.000Z", &utc) > 0;
}
}  // namespace

esp_err_t CloudClient::configure(const char* url, const char* device_id, const char* credential) {
  if (url == nullptr || device_id == nullptr || credential == nullptr ||
      std::strncmp(url, "https://", 8) != 0 || std::strlen(url) >= 192 ||
      std::strlen(device_id) >= 65 || std::strlen(credential) >= 129) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_str(handle, "url", url);
  if (err == ESP_OK) err = nvs_set_str(handle, "device_id", device_id);
  if (err == ESP_OK) err = nvs_set_str(handle, "credential", credential);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t CloudClient::start(WifiManager* wifi, PowerActuator* actuator) {
  if (wifi == nullptr || actuator == nullptr || wifi_ == wifi) return ESP_ERR_INVALID_ARG;
  wifi_ = wifi;
  actuator_ = actuator;
  if (!load_config()) {
    ESP_LOGI(kTag, "cloud client disabled; no cloud configuration");
    return ESP_OK;
  }
  return xTaskCreate(&task_entry, "cloud-client", 6144, this, 3, nullptr) == pdPASS
             ? ESP_OK
             : ESP_ERR_NO_MEM;
}

bool CloudClient::load_config() {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  const bool valid = read_string(handle, "url", base_url_, sizeof(base_url_)) &&
                     read_string(handle, "device_id", device_id_, sizeof(device_id_)) &&
                     read_string(handle, "credential", credential_, sizeof(credential_));
  nvs_close(handle);
  return valid && std::strncmp(base_url_, "https://", 8) == 0 && device_id_[0] != '\0' && credential_[0] != '\0';
}

void CloudClient::task_entry(void* arg) { static_cast<CloudClient*>(arg)->task_loop(); }

void CloudClient::task_loop() {
  while (true) {
    if (wifi_->is_station_connected()) poll_once();
    vTaskDelay(kPollInterval);
  }
}

bool CloudClient::poll_once() {
  char seen_url[384];
  std::snprintf(seen_url, sizeof(seen_url), "%s/api/device/%s/seen", base_url_, device_id_);
  esp_http_client_config_t seen_config{};
  seen_config.url = seen_url;
  seen_config.timeout_ms = 4000;
  seen_config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t seen_client = esp_http_client_init(&seen_config);
  if (seen_client == nullptr) return false;
  char auth[196];
  std::snprintf(auth, sizeof(auth), "Bearer %s", credential_);
  esp_http_client_set_header(seen_client, "Authorization", auth);
  const esp_err_t seen_err = esp_http_client_perform(seen_client);
  const int seen_status = esp_http_client_get_status_code(seen_client);
  esp_http_client_cleanup(seen_client);
  if (seen_err != ESP_OK || seen_status != 200) return false;

  char url[384];
  std::snprintf(url, sizeof(url), "%s/api/device/%s/commands/next", base_url_, device_id_);
  esp_http_client_config_t config{};
  config.url = url;
  config.timeout_ms = 4000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return false;
  std::snprintf(auth, sizeof(auth), "Bearer %s", credential_);
  esp_http_client_set_header(client, "Authorization", auth);
  const esp_err_t open_err = esp_http_client_open(client, 0);
  if (open_err != ESP_OK) { esp_http_client_cleanup(client); return false; }
  esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  char body[768]{};
  const int length = esp_http_client_read_response(client, body, sizeof(body) - 1);
  bool success = status == 200 && length > 0;
  if (success) {
    char command_id[40]{};
    char* id = std::strstr(body, "\"commandId\":\"");
    if (id != nullptr) {
      id += 13;
      char* end = std::strchr(id, '"');
      if (end != nullptr && end - id < static_cast<int>(sizeof(command_id))) {
        std::memcpy(command_id, id, end - id);
        const PressResult result = actuator_->request_press(command_id);
        ESP_LOGI(kTag, "cloud command result=%d", static_cast<int>(result));
        const char* status_text = result == PressResult::kAccepted ? "executed" :
                                  result == PressResult::kDuplicate ? "rejected" : "rejected";
        char result_url[384];
        std::snprintf(result_url, sizeof(result_url), "%s/api/device/%s/commands/result", base_url_, device_id_);
        esp_http_client_config_t result_config{};
        result_config.url = result_url;
        result_config.timeout_ms = 4000;
        esp_http_client_handle_t result_client = esp_http_client_init(&result_config);
        if (result_client != nullptr) {
          esp_http_client_set_header(result_client, "Authorization", auth);
          esp_http_client_set_header(result_client, "Content-Type", "application/json");
          char completed_at[32];
          if (!now_iso(completed_at)) {
            ESP_LOGW(kTag, "command executed but system time is not synchronized");
            esp_http_client_cleanup(result_client);
            return false;
          }
          char result_body[256];
          std::snprintf(result_body, sizeof(result_body),
                        "{\"protocolVersion\":1,\"commandId\":\"%s\",\"status\":\"%s\",\"completedAt\":\"%s\"}",
                        command_id, status_text, completed_at);
          esp_http_client_set_post_field(result_client, result_body, std::strlen(result_body));
          esp_http_client_set_method(result_client, HTTP_METHOD_POST);
          esp_http_client_perform(result_client);
          esp_http_client_cleanup(result_client);
        }
      }
    }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return success;
}

}  // namespace pc_power
