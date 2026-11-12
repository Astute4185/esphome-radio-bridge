#pragma once
#include "uart_mode.h"
namespace esphome::serial_radio_gateway::uart_modes {
class FourWireMode final : public UartModeStrategy {
 public:
  UartModeId id() const override { return UartModeId::FOUR_WIRE; }
  bool apply(UartHardware& hw, const UartPinout& p) const override {
    if (p.rts < 0 || p.cts < 0 || p.rts_threshold == 0) return false;
    return hw.set_flow_control(false, 0) && hw.set_pins(p.tx, p.rx, p.rts, p.cts) &&
           hw.set_flow_control(true, p.rts_threshold);
  }
};
}  // namespace esphome::serial_radio_gateway::uart_modes
