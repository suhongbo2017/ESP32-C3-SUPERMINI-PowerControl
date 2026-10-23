#pragma once

#include <atomic>
#include <mutex>

#include "esp_err.h"
#include "ping/ping_sock.h"

#include "wifi_manager.hpp"

namespace pc_power {

enum class Reachability { kUnknown, kReachable, kUnreachable };

class ReachabilityMonitor {
 public:
  esp_err_t start(WifiManager* wifi);
  esp_err_t configure_target(const char* host);
  esp_err_t get_target(char* output, size_t output_size) const;
  Reachability state() const { return state_.load(); }
  uint32_t seconds_until_next_probe() const;

 private:
  static void task_entry(void* arg);
  static void ping_success(esp_ping_handle_t handle, void* arg);
  static void ping_timeout(esp_ping_handle_t handle, void* arg);
  static void ping_end(esp_ping_handle_t handle, void* arg);
  void run();

  char target_[64]{};
  mutable std::mutex target_mutex_;
  std::atomic<uint32_t> target_generation_{0};
  std::atomic<uint32_t> active_generation_{0};
  std::atomic<Reachability> state_{Reachability::kUnknown};
  std::atomic<bool> ping_active_{false};
  std::atomic<TickType_t> next_probe_tick_{0};
  bool started_ = false;
  TaskHandle_t task_handle_ = nullptr;
  WifiManager* wifi_ = nullptr;
};

}  // namespace pc_power
