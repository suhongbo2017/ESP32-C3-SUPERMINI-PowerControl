#include "cloud_client.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "cloud-client";
constexpr char kNamespace[] = "cloud_config";
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(5000);
CloudClient* g_client = nullptr;

bool read_string(nvs_handle_t handle, const char* key, char* output, size_t size) {
  size_t length = size;
  return nvs_get_str(handle, key, output, &length) == ESP_OK;
}
struct PairResponse {
  char body[512]{};
  size_t length = 0;
  bool overflow = false;
};
esp_err_t pair_http_event(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == nullptr || event->data_len <= 0) return ESP_OK;
  auto* response = static_cast<PairResponse*>(event->user_data);
  if (response->length + event->data_len >= sizeof(response->body)) { response->overflow = true; return ESP_FAIL; }
  std::memcpy(response->body + response->length, event->data, event->data_len);
  response->length += event->data_len;
  response->body[response->length] = '\0';
  return ESP_OK;
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
  if (err == ESP_OK && g_client != nullptr) g_client->reload_requested_ = true;
  return err;
}

esp_err_t CloudClient::pair(const char* url, const char* device_id, const char* pairing_code) {
  if (url == nullptr || device_id == nullptr || pairing_code == nullptr ||
      std::strncmp(url, "https://", 8) != 0 || std::strlen(url) >= 192 ||
      std::strlen(device_id) < 3 || std::strlen(device_id) >= 65 ||
      std::strlen(pairing_code) < 16 || std::strlen(pairing_code) >= 65) return ESP_ERR_INVALID_ARG;
  for (const char* p = device_id; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return ESP_ERR_INVALID_ARG;
  for (const char* p = pairing_code; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) return ESP_ERR_INVALID_ARG;

  char pair_url[240];
  std::snprintf(pair_url, sizeof(pair_url), "%s/api/device/pair", url);
  PairResponse response{};
  esp_http_client_config_t config{};
  config.url = pair_url;
  config.timeout_ms = 12000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.event_handler = pair_http_event;
  config.user_data = &response;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return ESP_ERR_NO_MEM;
  char request_body[256];
  std::snprintf(request_body, sizeof(request_body),
                "{\"protocolVersion\":1,\"deviceId\":\"%s\",\"pairingCode\":\"%s\"}", device_id, pairing_code);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, request_body, std::strlen(request_body));
  esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  std::memset(request_body, 0, sizeof(request_body));
  if (err != ESP_OK || status != 201 || response.overflow) {
    std::memset(response.body, 0, sizeof(response.body));
    ESP_LOGW(kTag, "cloud pairing request failed: err=%s status=%d", esp_err_to_name(err), status);
    return err != ESP_OK ? err : ESP_FAIL;
  }
  cJSON* json = cJSON_Parse(response.body);
  std::memset(response.body, 0, sizeof(response.body));
  if (json == nullptr) return ESP_ERR_INVALID_RESPONSE;
  const cJSON* returned_id = cJSON_GetObjectItemCaseSensitive(json, "deviceId");
  const cJSON* credential = cJSON_GetObjectItemCaseSensitive(json, "credential");
  const bool response_valid = cJSON_IsString(returned_id) && std::strcmp(returned_id->valuestring, device_id) == 0 &&
                              cJSON_IsString(credential) && credential->valuestring[0] != '\0' &&
                              std::strlen(credential->valuestring) < 129;
  err = response_valid ? configure(url, device_id, credential->valuestring) : ESP_ERR_INVALID_RESPONSE;
  cJSON_Delete(json);
  if (err == ESP_OK && g_client != nullptr) g_client->reload_requested_ = true;
  return err;
}

esp_err_t CloudClient::start(WifiManager* wifi, PowerActuator* actuator,
                             ReachabilityMonitor* reachability,
                             EnvironmentSensor* environment,
                             PowerLedMonitor* power_led) {
  if (wifi == nullptr || actuator == nullptr || reachability == nullptr ||
      environment == nullptr || power_led == nullptr || wifi_ == wifi) return ESP_ERR_INVALID_ARG;
  wifi_ = wifi;
  actuator_ = actuator;
  reachability_ = reachability;
  environment_ = environment;
  power_led_ = power_led;
  g_client = this;
  configured_ = load_config();
  if (!configured_) ESP_LOGI(kTag, "cloud client waiting for web configuration");
  return xTaskCreate(&task_entry, "cloud-client", 12288, this, 3, nullptr) == pdPASS
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
  const bool configured = valid && std::strncmp(base_url_, "https://", 8) == 0 && device_id_[0] != '\0' && credential_[0] != '\0';
  if (!configured) { std::memset(credential_, 0, sizeof(credential_)); }
  return configured;
}

void CloudClient::task_entry(void* arg) { static_cast<CloudClient*>(arg)->task_loop(); }

void CloudClient::task_loop() {
  while (true) {
    if (reload_requested_) {
      reload_requested_ = false;
      configured_ = load_config();
      online_ = false;
      ESP_LOGI(kTag, "configuration %s", configured_ ? "loaded" : "disabled");
    }
    if (configured_ && wifi_->is_station_connected()) {
      online_ = poll_once();
    } else {
      online_ = false;
    }
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
  char ssid[33]{};
  if (wifi_->get_station_ssid(ssid, sizeof(ssid)) != ESP_OK) std::strcpy(ssid, "unavailable");
  esp_netif_ip_info_t ip_info{};
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  char device_ip[16] = "unavailable";
  if (netif != nullptr && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
    std::snprintf(device_ip, sizeof(device_ip), IPSTR, IP2STR(&ip_info.ip));
  }
  char host_ip[64]{};
  reachability_->get_target(host_ip, sizeof(host_ip));
  float temperature = 0.0f;
  float humidity = 0.0f;
  const bool sensor_available = environment_->read(&temperature, &humidity);
  const char* reachability = reachability_->state() == Reachability::kReachable ? "reachable" :
                              reachability_->state() == Reachability::kUnreachable ? "unreachable" : "unknown";
  const char* led_state = power_led_->state() == PowerLedState::kHigh ? "high" :
                          power_led_->state() == PowerLedState::kBlinking ? "blinking" : "low";
  char reported_at[32];
  if (!now_iso(reported_at)) std::strcpy(reported_at, "1970-01-01T00:00:00.000Z");
  char temperature_text[16] = "null";
  char humidity_text[16] = "null";
  if (sensor_available) {
    std::snprintf(temperature_text, sizeof(temperature_text), "%.1f", temperature);
    std::snprintf(humidity_text, sizeof(humidity_text), "%.1f", humidity);
  }
  char snapshot[768];
  std::snprintf(snapshot, sizeof(snapshot),
                "{\"protocolVersion\":1,\"deviceId\":\"%s\",\"reportedAt\":\"%s\",\"deviceIp\":\"%s\",\"hostIp\":\"%s\",\"wifi\":\"%s\",\"ssid\":\"%s\",\"reachability\":\"%s\",\"sensorAvailable\":%s,\"temperature\":%s,\"humidity\":%s,\"powerLedState\":\"%s\"}",
                device_id_, reported_at, device_ip, host_ip,
                wifi_->is_station_connected() ? "connected" : "disconnected",
                ssid, reachability, sensor_available ? "true" : "false",
                temperature_text, humidity_text, led_state);
  esp_http_client_set_header(seen_client, "Content-Type", "application/json");
  esp_http_client_set_post_field(seen_client, snapshot, std::strlen(snapshot));
  esp_http_client_set_method(seen_client, HTTP_METHOD_POST);
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
    const bool is_power_press = std::strstr(body, "\"kind\":\"power_press\"") != nullptr;
    if (id != nullptr) {
      id += 13;
      char* end = std::strchr(id, '"');
      if (end != nullptr && end - id < static_cast<int>(sizeof(command_id))) {
        std::memcpy(command_id, id, end - id);
        const PressResult result = is_power_press
                                      ? actuator_->request_press(command_id)
                                      : PressResult::kInvalidCommand;
        ESP_LOGI(kTag, "cloud command result=%d", static_cast<int>(result));
        if (result == PressResult::kAccepted) {
          // request_press queues the pulse; wait until its 300 ms actuation window has elapsed before acknowledging execution.
          vTaskDelay(pdMS_TO_TICKS(PowerActuator::kPulseMs + 50));
        }
        const char* status_text = result == PressResult::kAccepted ? "executed" :
                                  result == PressResult::kDuplicate ? "rejected" : "failed";
        char result_url[384];
        std::snprintf(result_url, sizeof(result_url), "%s/api/device/%s/commands/result", base_url_, device_id_);
        esp_http_client_config_t result_config{};
        result_config.url = result_url;
        result_config.timeout_ms = 4000;
        result_config.crt_bundle_attach = esp_crt_bundle_attach;
        esp_http_client_handle_t result_client = esp_http_client_init(&result_config);
        if (result_client != nullptr) {
          esp_http_client_set_header(result_client, "Authorization", auth);
          esp_http_client_set_header(result_client, "Content-Type", "application/json");
          char completed_at[32];
          if (!now_iso(completed_at)) {
            ESP_LOGW(kTag, "command executed but system time is not synchronized");
            esp_http_client_cleanup(result_client);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
          }
          char result_body[256];
          std::snprintf(result_body, sizeof(result_body),
                        "{\"protocolVersion\":1,\"commandId\":\"%s\",\"status\":\"%s\",\"completedAt\":\"%s\"}",
                        command_id, status_text, completed_at);
          esp_http_client_set_post_field(result_client, result_body, std::strlen(result_body));
          esp_http_client_set_method(result_client, HTTP_METHOD_POST);
          const esp_err_t result_err = esp_http_client_perform(result_client);
          const int result_status = esp_http_client_get_status_code(result_client);
          if (result_err != ESP_OK || result_status < 200 || result_status >= 300) {
            ESP_LOGW(kTag, "command result upload failed: err=%s status=%d",
                     esp_err_to_name(result_err), result_status);
          }
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
