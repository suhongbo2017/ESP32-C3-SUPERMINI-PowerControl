#include "power_led_monitor.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "power-led";
constexpr gpio_num_t kGpio = GPIO_NUM_0;
constexpr TickType_t kSampleInterval = pdMS_TO_TICKS(100);
constexpr int kSamples = 10;
constexpr int kBlinkTransitions = 2;
}  // namespace

esp_err_t PowerLedMonitor::start() {
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << kGpio;
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  esp_err_t err = gpio_config(&config);
  if (err != ESP_OK) return err;
  if (xTaskCreate(&task_entry, "power-led", 2048, this, 3, nullptr) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(kTag, "power LED input ready on GPIO0; polarity is raw level");
  return ESP_OK;
}

void PowerLedMonitor::task_entry(void* arg) {
  static_cast<PowerLedMonitor*>(arg)->task_loop();
}

void PowerLedMonitor::task_loop() {
  bool previous = gpio_get_level(kGpio) != 0;
  while (true) {
    int high_count = previous ? 1 : 0;
    int transitions = 0;
    for (int i = 1; i < kSamples; ++i) {
      vTaskDelay(kSampleInterval);
      const bool current = gpio_get_level(kGpio) != 0;
      high_count += current ? 1 : 0;
      if (current != previous) ++transitions;
      previous = current;
    }
    const bool level = high_count >= (kSamples / 2);
    level_.store(level);
    state_.store(transitions >= kBlinkTransitions
                     ? PowerLedState::kBlinking
                     : (level ? PowerLedState::kHigh : PowerLedState::kLow));
  }
}

}  // namespace pc_power
