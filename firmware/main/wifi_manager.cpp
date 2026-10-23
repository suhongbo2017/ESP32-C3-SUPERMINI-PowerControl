#include "wifi_manager.hpp"

#include <cstdio>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "wifi-manager";
constexpr char kNamespace[] = "wifi_config";
constexpr char kSsidKey[] = "ssid";
constexpr char kPasswordKey[] = "password";
constexpr int kMaxRetries = 8;
constexpr char kDefaultSsid[] = "JHS";
constexpr char kDefaultPassword[] = "jhs16888";
WifiManager* g_manager = nullptr;
}  // namespace

esp_err_t WifiManager::start() {
  if (initialized_) return ESP_ERR_INVALID_STATE;
  ESP_RETURN_ON_ERROR(esp_netif_init(), kTag, "initialize network interface");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), kTag, "create event loop");
  esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  uint8_t mac[6]{};
  ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), kTag,
                      "read station MAC");
  char hostname[16]{};
  std::snprintf(hostname, sizeof(hostname), "PC-POWER-%02X%02X", mac[4], mac[5]);
  ESP_RETURN_ON_ERROR(esp_netif_set_hostname(sta_netif, hostname), kTag,
                      "set station hostname");
  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), kTag, "initialize Wi-Fi");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  &event_handler, this),
                      kTag, "register Wi-Fi handler");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  &event_handler, this),
                      kTag, "register IP handler");
  ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), kTag,
                      "set Wi-Fi storage");
  initialized_ = true;
  g_manager = this;
  return connect_saved_or_start_ap();
}

esp_err_t WifiManager::get_station_ssid(char* output, size_t output_size) const {
  if (output == nullptr || output_size == 0 || !station_connected_) {
    return ESP_ERR_INVALID_STATE;
  }
  wifi_ap_record_t record{};
  const esp_err_t err = esp_wifi_sta_get_ap_info(&record);
  if (err != ESP_OK) return err;
  const size_t length = strnlen(reinterpret_cast<const char*>(record.ssid),
                                sizeof(record.ssid));
  if (length >= output_size) return ESP_ERR_INVALID_SIZE;
  std::memcpy(output, record.ssid, length);
  output[length] = '\0';
  return ESP_OK;
}

esp_err_t WifiManager::configure(const char* ssid, const char* password) {
  if (ssid == nullptr || password == nullptr || ssid[0] == '\0' ||
      std::strlen(ssid) > 32 || std::strlen(password) < 8 ||
      std::strlen(password) > 63) {
    return ESP_ERR_INVALID_ARG;
  }
  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), kTag,
                      "open Wi-Fi config");
  esp_err_t err = nvs_set_str(handle, kSsidKey, ssid);
  if (err == ESP_OK) err = nvs_set_str(handle, kPasswordKey, password);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  if (err != ESP_OK) return err;
  if (initialized_) {
    ESP_RETURN_ON_ERROR(esp_wifi_stop(), kTag, "stop Wi-Fi");
    ready_ = false;
    station_connected_ = false;
    ap_started_ = false;
    retry_count_ = 0;
    return connect_saved_or_start_ap();
  }
  return ESP_OK;
}

esp_err_t WifiManager::connect_saved_or_start_ap() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(kTag, "no Wi-Fi credentials; using initial network %s", kDefaultSsid);
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), kTag,
                       "open default Wi-Fi config");
    err = nvs_set_str(handle, kSsidKey, kDefaultSsid);
    if (err == ESP_OK) err = nvs_set_str(handle, kPasswordKey, kDefaultPassword);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    return connect_saved_or_start_ap();
  }
  if (err != ESP_OK) return err;

  wifi_config_t config{};
  size_t ssid_size = 0;
  size_t password_size = 0;
  err = nvs_get_str(handle, kSsidKey, nullptr, &ssid_size);
  if (err == ESP_OK) {
    err = nvs_get_str(handle, kPasswordKey, nullptr, &password_size);
  }
  if (err == ESP_OK && (ssid_size < 2 || ssid_size > sizeof(config.sta.ssid) ||
                        password_size < 9 ||
                        password_size > sizeof(config.sta.password))) {
    err = ESP_ERR_INVALID_ARG;
  }
  if (err == ESP_OK) {
    err = nvs_get_str(handle, kSsidKey,
                      reinterpret_cast<char*>(config.sta.ssid), &ssid_size);
  }
  if (err == ESP_OK) {
    err = nvs_get_str(handle, kPasswordKey,
                      reinterpret_cast<char*>(config.sta.password),
                      &password_size);
  }
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH) {
    ESP_LOGI(kTag, "Wi-Fi credentials incomplete; using initial network %s",
             kDefaultSsid);
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), kTag,
                       "open default Wi-Fi config");
    err = nvs_set_str(handle, kSsidKey, kDefaultSsid);
    if (err == ESP_OK) err = nvs_set_str(handle, kPasswordKey, kDefaultPassword);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    return connect_saved_or_start_ap();
  }
  if (err == ESP_ERR_INVALID_ARG) {
    ESP_LOGW(kTag, "stored Wi-Fi credentials invalid; starting setup AP");
    return start_setup_ap();
  }
  if (err != ESP_OK) return err;

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (err == ESP_OK) err = esp_wifi_start();
  if (err == ESP_OK) err = esp_wifi_connect();
  return err;
}

esp_err_t WifiManager::start_setup_ap() {
  if (ap_started_) return ESP_OK;
  wifi_config_t config{};
  uint8_t mac[6]{};
  ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), kTag,
                      "read station MAC for setup SSID");
  std::snprintf(reinterpret_cast<char*>(config.ap.ssid), sizeof(config.ap.ssid),
                "PC-POWER-%02X%02X", mac[4], mac[5]);
  config.ap.ssid_len = std::strlen(reinterpret_cast<char*>(config.ap.ssid));
  config.ap.channel = 1;
  config.ap.max_connection = 2;
  config.ap.authmode = WIFI_AUTH_OPEN;

  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &config);
  if (err == ESP_OK) err = esp_wifi_start();
  if (err == ESP_OK) {
    ap_started_ = true;
    ready_ = true;
    station_connected_ = false;
    ESP_LOGW(kTag, "open setup AP started; SSID=%s",
             reinterpret_cast<char*>(config.ap.ssid));
  }
  return err;
}

void WifiManager::event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void*) {
  auto* self = static_cast<WifiManager*>(arg);
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    self->ready_ = true;
    self->station_connected_ = true;
    self->ap_started_ = false;
    self->retry_count_ = 0;
    ESP_LOGI(kTag, "station connected and got IP");
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    self->ready_ = false;
    self->station_connected_ = false;
    if (++self->retry_count_ < kMaxRetries) {
      esp_wifi_connect();
    } else {
      ESP_LOGW(kTag, "station retries exhausted; starting setup AP");
      esp_wifi_stop();
      self->start_setup_ap();
    }
  }
}

}  // namespace pc_power
