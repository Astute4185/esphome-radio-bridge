#pragma once

#include "uart_mode.h"

namespace esphome::esphome_radio_bridge::uart_modes {

// Experimental NORMAL-mode configurations. The driver continues to own the
// bootloader UART mode, so firmware updates never depend on the tested mapping.
enum class NormalUartExperiment : uint8_t { TWO_WIRE, FOUR_WIRE_PROFILE, FOUR_WIRE_SWAPPED };

constexpr UartModeId experiment_wire_mode(NormalUartExperiment mode) {
  return mode == NormalUartExperiment::TWO_WIRE ? UartModeId::TWO_WIRE : UartModeId::FOUR_WIRE;
}

constexpr UartPinout experiment_pinout(UartPinout pins, NormalUartExperiment mode) {
  if (mode == NormalUartExperiment::FOUR_WIRE_SWAPPED) {
    const auto rts = pins.rts;
    pins.rts = pins.cts;
    pins.cts = rts;
  }
  return pins;
}

constexpr bool experiment_supported(UartPinout pins, UartModeId boot_mode, NormalUartExperiment mode) {
  const auto trial = experiment_pinout(pins, mode);
  return valid_uart_pinout(trial, experiment_wire_mode(mode), boot_mode);
}

// No mode transitions while TCP owns the NCP, a probe is running, firmware
// flashing is active, or a bootloader/maintenance command is in progress.
constexpr bool can_switch_normal_uart(bool normal_mode, bool tcp_client_connected, bool probe_active,
                                      bool firmware_busy, bool flash_armed, bool failed) {
  return normal_mode && !tcp_client_connected && !probe_active && !firmware_busy && !flash_armed && !failed;
}

constexpr const char* experiment_option(NormalUartExperiment mode) {
  switch (mode) {
    case NormalUartExperiment::TWO_WIRE:
      return "2-wire (TX/RX)";
    case NormalUartExperiment::FOUR_WIRE_PROFILE:
      return "4-wire (profile RTS/CTS)";
    case NormalUartExperiment::FOUR_WIRE_SWAPPED:
      return "4-wire (swapped RTS/CTS)";
  }
  return "Invalid";
}

}  // namespace esphome::esphome_radio_bridge::uart_modes
