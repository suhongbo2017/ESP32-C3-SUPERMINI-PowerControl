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
  while (true) {
    int high_count = 0;
    for (int i = 0; i < kSamples; ++i) {
      high_count += gpio_get_level(kGpio) != 0 ? 1 : 0;
      vTaskDelay(kSampleInterval);
    }
    level_.store(high_count >= (kSamples / 2));
  }
}

}  // namespace pc_power
