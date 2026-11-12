#pragma once

namespace esphome::esphome_radio_bridge {
enum class Mode { NORMAL, BOOTLOADER };
enum class Command { RESET, ENTER_BOOTLOADER, EXIT_BOOTLOADER };

constexpr bool allowed(Mode mode, Command command, bool armed, bool tcp_connected) {
  switch (command) {
    case Command::RESET:
      return mode == Mode::NORMAL && !tcp_connected;
    case Command::ENTER_BOOTLOADER:
      return mode == Mode::NORMAL && armed && !tcp_connected;
    case Command::EXIT_BOOTLOADER:
      return mode == Mode::BOOTLOADER && !tcp_connected;
  }
  return false;
}

}  // namespace esphome::esphome_radio_bridge
