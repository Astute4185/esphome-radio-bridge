#pragma once

#include <cstdint>
#include <memory>

#include "byte_ring.h"
#include "drivers/board_factory.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "mode_policy.h"
#include "uart_modes/esp_idf_uart_bus.h"

namespace esphome::serial_radio_gateway {

class SerialRadioGateway : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_board_model(drivers::BoardModel model) { driver_ = drivers::create_board_driver(model); }
  void set_tcp_port(uint16_t port) { tcp_port_ = port; }
  void set_maintenance_timeout(uint32_t timeout_ms) { maintenance_timeout_ms_ = timeout_ms; }
  void set_connected_sensor(binary_sensor::BinarySensor* value) { connected_sensor_ = value; }
  void set_disconnects_sensor(sensor::Sensor* value) { disconnects_sensor_ = value; }
  void set_mode_sensor(text_sensor::TextSensor* value) { mode_sensor_ = value; }
  void set_armed_switch(switch_::Switch* value) { armed_switch_ = value; }

  void set_armed(bool armed) { armed_ = armed; }
  bool is_armed() const { return armed_; }
  void radio_reset();
  void enter_bootloader();
  void exit_bootloader();

 protected:
  bool configure_uart_(Mode new_mode);
  void open_listener_();
  void close_listener_();
  void close_client_();
  void accept_client_();
  void bridge_uart_();
  void bridge_tcp_();
  void send_tcp_();
  void publish_mode_();
  void mark_maintenance_activity_();
  void report_error_(const char* message);

  std::unique_ptr<drivers::BoardDriver> driver_;
  int listener_fd_{-1};
  int client_fd_{-1};
  uint16_t tcp_port_{6638};
  uint32_t maintenance_timeout_ms_{600000};
  uint32_t last_maintenance_activity_{0};
  uint32_t disconnects_{0};
  Mode mode_{Mode::NORMAL};
  bool armed_{false};
  uart_modes::EspIdfUartBus uart_{};
  bool error_{false};
  ByteRing<4096> uart_to_tcp_{};
  ByteRing<2048> tcp_to_uart_{};
  binary_sensor::BinarySensor* connected_sensor_{nullptr};
  sensor::Sensor* disconnects_sensor_{nullptr};
  text_sensor::TextSensor* mode_sensor_{nullptr};
  switch_::Switch* armed_switch_{nullptr};
};

class RadioResetButton : public button::Button {
 public:
  explicit RadioResetButton(SerialRadioGateway* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->radio_reset(); }
  SerialRadioGateway* parent_;
};

class EnterBootloaderButton : public button::Button {
 public:
  explicit EnterBootloaderButton(SerialRadioGateway* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->enter_bootloader(); }
  SerialRadioGateway* parent_;
};

class ExitBootloaderButton : public button::Button {
 public:
  explicit ExitBootloaderButton(SerialRadioGateway* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->exit_bootloader(); }
  SerialRadioGateway* parent_;
};

class FlashArmedSwitch : public switch_::Switch {
 public:
  explicit FlashArmedSwitch(SerialRadioGateway* parent) : parent_(parent) {}

 protected:
  void write_state(bool state) override {
    parent_->set_armed(state);
    this->publish_state(state);
  }
  SerialRadioGateway* parent_;
};

}  // namespace esphome::serial_radio_gateway
