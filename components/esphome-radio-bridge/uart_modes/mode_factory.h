#pragma once
#include "four_wire.h"
#include "two_wire.h"
namespace esphome::serial_radio_gateway::uart_modes {
inline const UartModeStrategy &mode_strategy(UartModeId id) {
  static const TwoWireMode two_wire;
  static const FourWireMode four_wire;
  return id == UartModeId::FOUR_WIRE ? static_cast<const UartModeStrategy &>(four_wire)
                                     : static_cast<const UartModeStrategy &>(two_wire);
}
}  // namespace esphome::serial_radio_gateway::uart_modes
