#include <cassert>
#include <string>

#include "normal_uart_mode.h"
#include "tubeszb_mgm210_poe_2023_profile.h"

using namespace esphome::esphome_radio_bridge::uart_modes;
using esphome::esphome_radio_bridge::drivers::TUBESZB_MGM210_POE_2023;

int main() {
  constexpr auto board = TUBESZB_MGM210_POE_2023;
  static_assert(board.normal_uart_mode == UartModeId::FOUR_WIRE);
  static_assert(board.bootloader_uart_mode == UartModeId::TWO_WIRE);

  static_assert(effective_normal_uart_mode(board.normal_uart_mode, NormalUartModeOverride::DRIVER) ==
                UartModeId::FOUR_WIRE);
  static_assert(effective_normal_uart_mode(board.normal_uart_mode, NormalUartModeOverride::TWO_WIRE) ==
                UartModeId::TWO_WIRE);
  static_assert(effective_normal_uart_mode(UartModeId::TWO_WIRE, NormalUartModeOverride::FOUR_WIRE) ==
                UartModeId::FOUR_WIRE);
  static_assert(valid_uart_pinout(board.uart, UartModeId::TWO_WIRE, board.bootloader_uart_mode));
  static_assert(valid_uart_pinout(board.uart, UartModeId::FOUR_WIRE, board.bootloader_uart_mode));

  // A board wired for only TX/RX may never be switched to four-wire.
  constexpr UartPinout two_wire_only{1, 4, 36, -1, -1, 0};
  static_assert(valid_uart_pinout(two_wire_only, UartModeId::TWO_WIRE, UartModeId::TWO_WIRE));
  static_assert(!valid_uart_pinout(two_wire_only, UartModeId::FOUR_WIRE, UartModeId::TWO_WIRE));
  assert(std::string(override_name(NormalUartModeOverride::DRIVER)) == "Driver default");
  assert(std::string(override_name(NormalUartModeOverride::TWO_WIRE)) == "YAML 2-wire");
  assert(std::string(override_name(NormalUartModeOverride::FOUR_WIRE)) == "YAML 4-wire");
  assert(std::string(operating_mode_name(UartModeId::FOUR_WIRE, false)) == "4-wire");
  assert(std::string(operating_mode_name(UartModeId::TWO_WIRE, false)) == "2-wire");
  assert(std::string(operating_mode_name(UartModeId::TWO_WIRE, true)) == "2-wire bootloader");
  assert(std::string(operating_mode_name(UartModeId::FOUR_WIRE, true)) == "4-wire bootloader");
}
