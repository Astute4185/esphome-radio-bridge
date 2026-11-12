#pragma once
#include "uart_mode.h"
namespace esphome::esphome_radio_bridge::uart_modes {
class TwoWireMode final : public UartModeStrategy {
 public:
  UartModeId id() const override { return UartModeId::TWO_WIRE; }
  bool apply(UartHardware& hw, const UartPinout& p) const override {
    // Disable handshaking before entering a two-wire bootloader.
    // -1 means UART_PIN_NO_CHANGE, not physical disconnection of a PCB trace.
    return hw.set_flow_control(false, 0) && hw.set_pins(p.tx, p.rx, -1, -1);
  }
};
}  // namespace esphome::esphome_radio_bridge::uart_modes
