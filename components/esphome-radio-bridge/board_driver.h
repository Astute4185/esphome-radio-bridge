#pragma once

#include <cstdint>

#include "radio_identification.h"
#include "uart_mode.h"

namespace esphome::esphome_radio_bridge::drivers {

// Firmware protocol is metadata, not an instruction to the gateway TCP bridge.
enum class FirmwareProtocol : uint8_t { NONE, GECKO_XMODEM_CRC };

// Immutable physical and operational contract. UART modes are selected by each driver.
struct BoardProfile {
  const char* id;
  uart_modes::UartPinout uart;
  uart_modes::UartModeId normal_uart_mode;
  uart_modes::UartModeId bootloader_uart_mode;
  int reset_gpio;  // -1 when no software-controlled reset is available
  int boot_gpio;   // -1 when GPIO bootloader entry is unavailable
  bool reset_active_low;
  bool boot_active_low;
  uint32_t normal_baud;
  uint32_t bootloader_baud;
  bool supports_bootloader;
  FirmwareProtocol firmware_protocol;
};

constexpr bool valid_profile(const BoardProfile& p) {
  if (p.normal_baud == 0 || p.bootloader_baud == 0 ||
      !uart_modes::valid_uart_pinout(p.uart, p.normal_uart_mode, p.bootloader_uart_mode))
    return false;
  const int controls[] = {p.reset_gpio, p.boot_gpio};
  for (int i = 0; i < 2; ++i) {
    if (controls[i] == -1) continue;
    if (controls[i] < 0 || controls[i] > 33 || controls[i] == p.uart.tx || controls[i] == p.uart.rx ||
        controls[i] == p.uart.rts || controls[i] == p.uart.cts)
      return false;
  }
  return p.reset_gpio < 0 || p.reset_gpio != p.boot_gpio;
}

// Platform GPIO operations are encapsulated by each concrete board driver.
// Core does not know about PA0, GPIO5, or any radio-specific boot sequence.
class BoardDriver {
 public:
  virtual ~BoardDriver() = default;
  virtual const BoardProfile& profile() const = 0;
  virtual bool setup() = 0;
  virtual bool reset_radio() = 0;
  virtual bool enter_bootloader() = 0;
  virtual bool exit_bootloader() = 0;
  // Optional driver-owned identification protocol; nullptr means unsupported.
  virtual identification::RadioProbe* identification_probe() { return nullptr; }
  virtual void shutdown() = 0;
};

}  // namespace esphome::esphome_radio_bridge::drivers
