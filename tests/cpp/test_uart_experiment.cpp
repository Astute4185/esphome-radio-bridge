#include <cassert>

#include "tubeszb_mgm210_poe_2023_profile.h"
#include "uart_experiment.h"

using namespace esphome::esphome_radio_bridge::uart_modes;

int main() {
  constexpr auto board = esphome::esphome_radio_bridge::drivers::TUBESZB_MGM210_POE_2023;
  constexpr auto two = experiment_pinout(board.uart, NormalUartExperiment::TWO_WIRE);
  constexpr auto profile = experiment_pinout(board.uart, NormalUartExperiment::FOUR_WIRE_PROFILE);
  constexpr auto swapped = experiment_pinout(board.uart, NormalUartExperiment::FOUR_WIRE_SWAPPED);
  static_assert(two.tx == 4 && two.rx == 36);
  static_assert(profile.rts == 2 && profile.cts == 13);
  static_assert(swapped.rts == 13 && swapped.cts == 2);
  static_assert(swapped.tx == 4 && swapped.rx == 36);
  static_assert(experiment_supported(board.uart, board.bootloader_uart_mode, NormalUartExperiment::TWO_WIRE));
  static_assert(
      experiment_supported(board.uart, board.bootloader_uart_mode, NormalUartExperiment::FOUR_WIRE_PROFILE));
  static_assert(
      experiment_supported(board.uart, board.bootloader_uart_mode, NormalUartExperiment::FOUR_WIRE_SWAPPED));
  constexpr UartPinout no_flow = {1, 4, 36, -1, -1, 0};
  static_assert(experiment_supported(no_flow, UartModeId::TWO_WIRE, NormalUartExperiment::TWO_WIRE));
  static_assert(
      !experiment_supported(no_flow, UartModeId::TWO_WIRE, NormalUartExperiment::FOUR_WIRE_PROFILE));
  static_assert(can_switch_normal_uart(true, false, false, false, false, false));
  static_assert(!can_switch_normal_uart(true, true, false, false, false, false));
  static_assert(!can_switch_normal_uart(true, false, true, false, false, false));
  static_assert(!can_switch_normal_uart(true, false, false, true, false, false));
  static_assert(!can_switch_normal_uart(true, false, false, false, true, false));
  static_assert(!can_switch_normal_uart(true, false, false, false, false, true));
  static_assert(!can_switch_normal_uart(false, false, false, false, false, false));
  assert(experiment_wire_mode(NormalUartExperiment::TWO_WIRE) == UartModeId::TWO_WIRE);
  assert(experiment_wire_mode(NormalUartExperiment::FOUR_WIRE_PROFILE) == UartModeId::FOUR_WIRE);
  assert(experiment_wire_mode(NormalUartExperiment::FOUR_WIRE_SWAPPED) == UartModeId::FOUR_WIRE);
}
