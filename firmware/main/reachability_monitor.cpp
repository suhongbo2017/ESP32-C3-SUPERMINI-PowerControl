#include "reachability_monitor.hpp"

#include <cstring>

#include "esp_check.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "ping/ping_sock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "reachability";
constexpr char kNamespace[] = "ping_config";
constexpr char kTargetKey[] = "target";
constexpr TickType_t kProbeInterval = pdMS_TO_TICKS(30000);

}  // namespace

esp_err_t ReachabilityMonitor::start(WifiManager* wifi) {
  if (started_ || wifi == nullptr) return ESP_ERR_INVALID_ARG;
  wifi_ = wifi;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_OK) {
    size_t length = sizeof(target_);
    err = nvs_get_str(handle, kTargetKey, target_, &length);
    nvs_close(handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
  } else if (err != ESP_ERR_NVS_NOT_FOUND) {
    return err;
  }
  if (xTaskCreate(&task_entry, "reachability", 4096, this, 3, nullptr) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  started_ = true;
  return ESP_OK;
}

esp_err_t ReachabilityMonitor::get_target(char* output, size_t output_size) const {
  if (output == nullptr || output_size == 0) return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(target_mutex_);
  const size_t length = std::strlen(target_);
  if (length >= output_size) return ESP_ERR_INVALID_SIZE;
  std::memcpy(output, target_, length + 1);
  return ESP_OK;
}

esp_err_t ReachabilityMonitor::configure_target(const char* host) {
  if (host == nullptr || std::strlen(host) == 0 || std::strlen(host) >= sizeof(target_)) {
    return ESP_ERR_INVALID_ARG;
  }
  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), kTag,
                      "open ping configuration");
  esp_err_t err = nvs_set_str(handle, kTargetKey, host);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  if (err != ESP_OK) return err;
  {
    std::lock_guard<std::mutex> lock(target_mutex_);
    std::strncpy(target_, host, sizeof(target_) - 1);
    target_[sizeof(target_) - 1] = '\0';
    ++target_generation_;
    state_.store(Reachability::kUnknown);
  }
  ESP_LOGI(kTag, "ping target configured");
  return ESP_OK;
}

uint32_t ReachabilityMonitor::seconds_until_next_probe() const {
  const TickType_t remaining = next_probe_tick_.load() - xTaskGetTickCount();
  return static_cast<uint32_t>(remaining / configTICK_RATE_HZ);
}

void ReachabilityMonitor::task_entry(void* arg) {
  static_cast<ReachabilityMonitor*>(arg)->run();
}

void ReachabilityMonitor::ping_success(esp_ping_handle_t, void*) {}

void ReachabilityMonitor::ping_timeout(esp_ping_handle_t, void*) {}

void ReachabilityMonitor::ping_end(esp_ping_handle_t handle, void* arg) {
  auto* self = static_cast<ReachabilityMonitor*>(arg);
  uint32_t replies = 0;
  esp_ping_get_profile(handle, ESP_PING_PROF_REPLY, &replies, sizeof(replies));
  const Reachability result = replies > 0 ? Reachability::kReachable
                                          : Reachability::kUnreachable;
  esp_ping_delete_session(handle);
  {
    std::lock_guard<std::mutex> lock(self->target_mutex_);
    if (self->active_generation_.load() == self->target_generation_.load()) {
      if (self->state_.exchange(result) != result) {
        ESP_LOGI(kTag, "reachability=%s",
                 result == Reachability::kReachable ? "reachable" : "unreachable");
      }
    }
  }
  self->ping_active_.store(false);
}

void ReachabilityMonitor::run() {
  next_probe_tick_.store(xTaskGetTickCount());
  while (true) {
    const TickType_t cycle_start = xTaskGetTickCount();
    next_probe_tick_.store(cycle_start + kProbeInterval);
    if (!wifi_->is_ready()) {
      if (state_.exchange(Reachability::kUnknown) != Reachability::kUnknown) {
        ESP_LOGI(kTag, "reachability=unknown (network unavailable)");
      }
      const TickType_t remaining = cycle_start + kProbeInterval - xTaskGetTickCount();
      if (remaining > 0) vTaskDelay(remaining);
      continue;
    }
    char target[sizeof(target_)];
    uint32_t target_generation = 0;
    {
      std::lock_guard<std::mutex> lock(target_mutex_);
      std::strncpy(target, target_, sizeof(target));
      target[sizeof(target) - 1] = '\0';
      target_generation = target_generation_.load();
    }
    if (target[0] == '\0') {
      if (state_.exchange(Reachability::kUnknown) != Reachability::kUnknown) {
        ESP_LOGI(kTag, "reachability=unknown (target not configured)");
      }
      vTaskDelay(kProbeInterval);
      continue;
    }

    ip_addr_t address{};
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* result = nullptr;
    if (getaddrinfo(target, nullptr, &hints, &result) != 0 || result == nullptr) {
      if (state_.exchange(Reachability::kUnknown) != Reachability::kUnknown) {
        ESP_LOGI(kTag, "reachability=unknown (target resolution failed)");
      }
      ESP_LOGW(kTag, "target resolution failed");
      const TickType_t remaining = cycle_start + kProbeInterval - xTaskGetTickCount();
      if (remaining > 0) vTaskDelay(remaining);
      continue;
    }
    auto* address4 = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    inet_addr_to_ip4addr(ip_2_ip4(&address), &address4->sin_addr);
    freeaddrinfo(result);

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = address;
    config.count = 3;
    config.interval_ms = 500;
    config.timeout_ms = 1000;
    active_generation_.store(target_generation);
    esp_ping_callbacks_t callbacks{};
    callbacks.cb_args = this;
    callbacks.on_ping_success = &ReachabilityMonitor::ping_success;
    callbacks.on_ping_timeout = &ReachabilityMonitor::ping_timeout;
    callbacks.on_ping_end = &ReachabilityMonitor::ping_end;
    esp_ping_handle_t session = nullptr;
    esp_err_t ping_err = esp_ping_new_session(&config, &callbacks, &session);
    if (ping_err != ESP_OK || session == nullptr) {
      state_.store(Reachability::kUnknown);
      const TickType_t remaining = cycle_start + kProbeInterval - xTaskGetTickCount();
      if (remaining > 0) vTaskDelay(remaining);
      continue;
    }
    ping_active_.store(true);
    if (esp_ping_start(session) != ESP_OK) {
      if (state_.exchange(Reachability::kUnknown) != Reachability::kUnknown) {
        ESP_LOGI(kTag, "reachability=unknown (ping start failed)");
      }
      ping_active_.store(false);
      esp_ping_delete_session(session);
      const TickType_t remaining = cycle_start + kProbeInterval - xTaskGetTickCount();
      if (remaining > 0) vTaskDelay(remaining);
      continue;
    }
    while (ping_active_.load()) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    const TickType_t remaining = cycle_start + kProbeInterval - xTaskGetTickCount();
    if (remaining > 0) vTaskDelay(remaining);
  }
}

}  // namespace pc_power
