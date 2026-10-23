#include "power_actuator.hpp"

#include <cstring>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace pc_power {
namespace {
constexpr char kTag[] = "power-actuator";
constexpr size_t kCommandIdLength = 36;

struct PressRequest {
  char command_id[37];
};
}  // namespace

esp_err_t PowerActuator::start() {
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << kGpio;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;

  esp_err_t err = gpio_config(&config);
  if (err != ESP_OK) {
    return err;
  }

  // Establish the safe idle level before creating the worker task.
  err = gpio_set_level(static_cast<gpio_num_t>(kGpio), 0);
  if (err != ESP_OK) {
    return err;
  }

  mutex_ = xSemaphoreCreateMutex();
  queue_ = xQueueCreate(1, sizeof(PressRequest));
  if (mutex_ == nullptr || queue_ == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  BaseType_t created = xTaskCreate(&PowerActuator::worker_entry, "power_press",
                                  2048, this, 5, nullptr);
  if (created != pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(kTag, "GPIO%d ready; idle=LOW pulse=%lums", kGpio,
           static_cast<unsigned long>(kPulseMs));
  return ESP_OK;
}

PressResult PowerActuator::request_press(const char* command_id) {
  if (command_id == nullptr || std::strlen(command_id) != kCommandIdLength) {
    return PressResult::kInvalidCommand;
  }

  PressRequest request{};
  std::strncpy(request.command_id, command_id, sizeof(request.command_id) - 1);

  auto* mutex = static_cast<SemaphoreHandle_t>(mutex_);
  auto* queue = static_cast<QueueHandle_t>(queue_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return PressResult::kBusy;
  }

  if (has_seen(command_id)) {
    xSemaphoreGive(mutex);
    return PressResult::kDuplicate;
  }
  if (busy_ || uxQueueMessagesWaiting(queue) != 0) {
    xSemaphoreGive(mutex);
    return PressResult::kBusy;
  }

  remember(command_id);
  busy_ = true;
  xSemaphoreGive(mutex);

  if (xQueueSend(queue, &request, 0) != pdTRUE) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    busy_ = false;
    xSemaphoreGive(mutex);
    return PressResult::kBusy;
  }
  return PressResult::kAccepted;
}

void PowerActuator::worker_entry(void* context) {
  static_cast<PowerActuator*>(context)->worker_loop();
}

void PowerActuator::worker_loop() {
  PressRequest request{};
  auto* mutex = static_cast<SemaphoreHandle_t>(mutex_);
  auto* queue = static_cast<QueueHandle_t>(queue_);

  while (true) {
    if (xQueueReceive(queue, &request, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    ESP_LOGI(kTag, "executing command=%s", request.command_id);
    gpio_set_level(static_cast<gpio_num_t>(kGpio), 1);
    vTaskDelay(pdMS_TO_TICKS(kPulseMs));
    gpio_set_level(static_cast<gpio_num_t>(kGpio), 0);

    xSemaphoreTake(mutex, portMAX_DELAY);
    busy_ = false;
    xSemaphoreGive(mutex);
    ESP_LOGI(kTag, "completed command=%s; GPIO%d=LOW", request.command_id,
             kGpio);
  }
}

bool PowerActuator::has_seen(const char* command_id) const {
  for (size_t i = 0; i < remembered_count_; ++i) {
    if (std::strncmp(remembered_ids_[i], command_id,
                     sizeof(remembered_ids_[i])) == 0) {
      return true;
    }
  }
  return false;
}

void PowerActuator::remember(const char* command_id) {
  if (remembered_count_ < kRememberedCommandCount) {
    std::strncpy(remembered_ids_[remembered_count_], command_id,
                 sizeof(remembered_ids_[remembered_count_]) - 1);
    ++remembered_count_;
    return;
  }

  for (size_t i = 1; i < kRememberedCommandCount; ++i) {
    std::strncpy(remembered_ids_[i - 1], remembered_ids_[i],
                 sizeof(remembered_ids_[i - 1]) - 1);
  }
  std::strncpy(remembered_ids_[kRememberedCommandCount - 1], command_id,
               sizeof(remembered_ids_[0]) - 1);
}

}  // namespace pc_power
