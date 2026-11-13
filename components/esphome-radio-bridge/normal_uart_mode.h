#pragma once
#include <cstdint>

#include "uart_mode.h"

namespace esphome::esphome_radio_bridge::uart_modes {

// YAML may override the driver's application UART strategy. Bootloader mode
// always remains driver-owned to preserve recovery on two-wire bootloaders.
enum class NormalUartModeOverride : uint8_t { DRIVER, TWO_WIRE, FOUR_WIRE };

constexpr UartModeId effective_normal_uart_mode(UartModeId driver_default, NormalUartModeOverride selection) {
  switch (selection) {
    case NormalUartModeOverride::TWO_WIRE:
      return UartModeId::TWO_WIRE;
    case NormalUartModeOverride::FOUR_WIRE:
      return UartModeId::FOUR_WIRE;
    case NormalUartModeOverride::DRIVER:
    default:
      return driver_default;
  }
}

constexpr const char* override_name(NormalUartModeOverride selection) {
  switch (selection) {
    case NormalUartModeOverride::TWO_WIRE:
      return "YAML 2-wire";
    case NormalUartModeOverride::FOUR_WIRE:
      return "YAML 4-wire";
    case NormalUartModeOverride::DRIVER:
    default:
      return "Driver default";
  }
}

// Stable Home Assistant Operating Mode text, regardless of the selected driver.
constexpr const char* operating_mode_name(UartModeId mode, bool bootloader) {
  if (bootloader) return mode == UartModeId::FOUR_WIRE ? "4-wire bootloader" : "2-wire bootloader";
  return mode == UartModeId::FOUR_WIRE ? "4-wire" : "2-wire";
}
}  // namespace esphome::esphome_radio_bridge::uart_modes
