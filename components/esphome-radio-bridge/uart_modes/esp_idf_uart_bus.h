#pragma once
#include <cstddef>
#include <cstdint>

#include "driver/uart.h"
#include "esp_err.h"
#include "mode_factory.h"
namespace esphome::serial_radio_gateway::uart_modes {
// Owns the ESP-IDF UART. The gateway does not call uart_* functions directly.
class EspIdfUartBus final : private UartHardware {
 public:
  bool start(const UartPinout& pins, uint32_t baud, UartModeId normal, UartModeId bootloader) {
    if (ready_ || baud == 0 || !valid_uart_pinout(pins, normal, bootloader)) return false;
    pins_ = pins;
    port_ = static_cast<uart_port_t>(pins.port);
    uart_config_t cfg{};
    cfg.baud_rate = baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    if (uart_param_config(port_, &cfg) != ESP_OK ||
        uart_driver_install(port_, 4096, 2048, 0, nullptr, 0) != ESP_OK)
      return false;
    ready_ = true;
    if (!configure(baud, normal)) {
      stop();
      return false;
    }
    return true;
  }
  bool configure(uint32_t baud, UartModeId mode) {
    if (!ready_ || baud == 0 || !valid_uart_mode(mode) || uart_set_baudrate(port_, baud) != ESP_OK ||
        !mode_strategy(mode).apply(*this, pins_))
      return false;
    flush_input();
    return true;
  }
  int read(uint8_t* data, size_t size) { return ready_ ? uart_read_bytes(port_, data, size, 0) : -1; }
  int write(const uint8_t* data, size_t size) { return ready_ ? uart_write_bytes(port_, data, size) : -1; }
  void flush_input() {
    if (ready_) uart_flush_input(port_);
  }
  bool ready() const { return ready_; }
  void stop() {
    if (!ready_) return;
    uart_set_hw_flow_ctrl(port_, UART_HW_FLOWCTRL_DISABLE, 0);
    uart_driver_delete(port_);
    ready_ = false;
  }

 private:
  bool set_pins(int tx, int rx, int rts, int cts) override {
    return uart_set_pin(port_, tx, rx, rts, cts) == ESP_OK;
  }
  bool set_flow_control(bool enabled, uint8_t threshold) override {
    return uart_set_hw_flow_ctrl(port_, enabled ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE,
                                 threshold) == ESP_OK;
  }
  UartPinout pins_{};
  uart_port_t port_{UART_NUM_1};
  bool ready_{false};
};
}  // namespace esphome::serial_radio_gateway::uart_modes
