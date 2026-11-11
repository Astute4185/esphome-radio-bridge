#pragma once

#include "board_driver.h"
#include "tubeszb_mgm210_poe_2023_profile.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::serial_radio_gateway::drivers {

class TubesZbMgm210Poe2023Driver final : public BoardDriver {
 public:
  const BoardProfile &profile() const override { return TUBESZB_MGM210_POE_2023; }

  bool setup() override {
    // Preload deasserted levels before enabling output to reduce control-line glitches.
    if (!this->drive_reset_(false) || !this->drive_boot_(false))
      return false;
    gpio_config_t pins{};
    pins.pin_bit_mask = (1ULL << TUBESZB_MGM210_POE_2023.reset_gpio) |
                        (1ULL << TUBESZB_MGM210_POE_2023.boot_gpio);
    pins.mode = GPIO_MODE_OUTPUT;
    pins.pull_up_en = GPIO_PULLUP_DISABLE;
    pins.pull_down_en = GPIO_PULLDOWN_DISABLE;
    pins.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&pins) == ESP_OK && this->drive_reset_(false) && this->drive_boot_(false);
  }

  bool reset_radio() override { return this->pulse_reset_(); }

  bool enter_bootloader() override {
    if (!this->drive_boot_(true))
      return false;
    bool ok = this->pulse_reset_();
    vTaskDelay(pdMS_TO_TICKS(100));
    return this->drive_boot_(false) && ok;
  }

  bool exit_bootloader() override {
    return this->drive_boot_(false) && this->pulse_reset_();
  }

  void shutdown() override { this->drive_boot_(false); }

 private:
  bool drive_(int pin, bool active, bool active_low) {
    const uint32_t level = (active == active_low) ? 0 : 1;
    return gpio_set_level(static_cast<gpio_num_t>(pin), level) == ESP_OK;
  }
  bool drive_reset_(bool active) {
    return this->drive_(TUBESZB_MGM210_POE_2023.reset_gpio, active,
                        TUBESZB_MGM210_POE_2023.reset_active_low);
  }
  bool drive_boot_(bool active) {
    return this->drive_(TUBESZB_MGM210_POE_2023.boot_gpio, active,
                        TUBESZB_MGM210_POE_2023.boot_active_low);
  }
  bool pulse_reset_() {
    if (!this->drive_reset_(true))
      return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return this->drive_reset_(false);
  }
};

}  // namespace esphome::serial_radio_gateway::drivers
