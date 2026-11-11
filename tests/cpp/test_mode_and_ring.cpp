#include "byte_ring.h"
#include "mode_policy.h"
#include "drivers/board_driver.h"
#include "drivers/tubeszb_mgm210_poe_2023_profile.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>

using namespace esphome::serial_radio_gateway;

static void test_policy() {
  static_assert(allowed(Mode::NORMAL, Command::ENTER_BOOTLOADER, true, false));
  static_assert(!allowed(Mode::NORMAL, Command::ENTER_BOOTLOADER, false, false));
  static_assert(!allowed(Mode::NORMAL, Command::ENTER_BOOTLOADER, true, true));
  static_assert(allowed(Mode::NORMAL, Command::RESET, false, false));
  static_assert(!allowed(Mode::NORMAL, Command::RESET, true, true));
  static_assert(!allowed(Mode::BOOTLOADER, Command::RESET, true, false));
  static_assert(!allowed(Mode::NORMAL, Command::EXIT_BOOTLOADER, true, false));
  static_assert(allowed(Mode::BOOTLOADER, Command::EXIT_BOOTLOADER, false, false));
  static_assert(!allowed(Mode::BOOTLOADER, Command::EXIT_BOOTLOADER, false, true));
}

class MockBoardDriver final : public esphome::serial_radio_gateway::drivers::BoardDriver {
 public:
  explicit MockBoardDriver(const esphome::serial_radio_gateway::drivers::BoardProfile &p) : profile_(p) {}
  const esphome::serial_radio_gateway::drivers::BoardProfile &profile() const override { return profile_; }
  bool setup() override { ++calls_; return true; }
  bool reset_radio() override { ++calls_; return true; }
  bool enter_bootloader() override { ++calls_; return true; }
  bool exit_bootloader() override { ++calls_; return true; }
  void shutdown() override { ++calls_; }
  int calls_{0};
 private:
  const esphome::serial_radio_gateway::drivers::BoardProfile &profile_;
};

static void test_mock_driver_interface() {
  using namespace esphome::serial_radio_gateway::drivers;
  MockBoardDriver device(TUBESZB_MGM210_POE_2023);
  BoardDriver &d = device;
  assert(d.setup());
  assert(d.reset_radio());
  assert(d.enter_bootloader());
  assert(d.exit_bootloader());
  d.shutdown();
  assert(device.calls_ == 5);
  assert(d.profile().uart.rts == 2);
}

static void test_profile() {
  using namespace esphome::serial_radio_gateway::drivers;
  using uart_modes::UartModeId;
  constexpr auto mgm = TUBESZB_MGM210_POE_2023;
  static_assert(valid_profile(mgm));
  static_assert(mgm.uart.port == 1 && mgm.uart.tx == 4 && mgm.uart.rx == 36);
  static_assert(mgm.uart.rts == 2 && mgm.uart.cts == 13);
  static_assert(mgm.reset_gpio == 5 && mgm.boot_gpio == 16);
  static_assert(mgm.normal_uart_mode == UartModeId::FOUR_WIRE);
  static_assert(mgm.bootloader_uart_mode == UartModeId::TWO_WIRE);
  static_assert(mgm.normal_baud == mgm.bootloader_baud);
  static_assert(mgm.firmware_protocol == FirmwareProtocol::GECKO_XMODEM_CRC);
  // Mock profiles are not registered for production.
  constexpr BoardProfile two = {
      "two_wire_mock", {2, 17, 16, -1, -1, 0},
      UartModeId::TWO_WIRE, UartModeId::TWO_WIRE,
      -1, -1, false, false, 230400, 57600, false, FirmwareProtocol::NONE};
  constexpr BoardProfile four = {
      "four_wire_mock", {2, 17, 16, 4, 5, 96},
      UartModeId::FOUR_WIRE, UartModeId::FOUR_WIRE,
      18, 19, true, false, 230400, 57600, true, FirmwareProtocol::NONE};
  constexpr BoardProfile hybrid = {
      "hybrid_mock", {2, 17, 16, 4, 5, 96},
      UartModeId::TWO_WIRE, UartModeId::FOUR_WIRE,
      18, 19, true, false, 230400, 57600, true, FirmwareProtocol::NONE};
  constexpr BoardProfile invalid = {
      "missing_rts_mock", {2, 17, 16, -1, -1, 0},
      UartModeId::FOUR_WIRE, UartModeId::TWO_WIRE,
      -1, -1, false, false, 230400, 57600, false, FirmwareProtocol::NONE};
  static_assert(valid_profile(two));
  static_assert(valid_profile(four));
  static_assert(valid_profile(hybrid));
  static_assert(!valid_profile(invalid));
}

static void test_ring_wrap_and_backpressure() {
  ByteRing<5> ring;
  const uint8_t first[] = {1, 2, 3, 4};
  assert(ring.push(first, sizeof(first)) == sizeof(first));
  assert(ring.size() == 4);
  assert(ring.head_size() == 4);
  assert(std::memcmp(ring.head_data(), first, 4) == 0);
  ring.consume(3);
  const uint8_t second[] = {5, 6, 7, 8, 9};
  assert(ring.push(second, sizeof(second)) == 4); // only four free
  assert(ring.size() == 5);
  const uint8_t expected1[] = {4, 5};
  assert(ring.head_size() == 2);
  assert(std::memcmp(ring.head_data(), expected1, 2) == 0);
  ring.consume(2);
  const uint8_t expected2[] = {6, 7, 8};
  assert(ring.head_size() == 3);
  assert(std::memcmp(ring.head_data(), expected2, 3) == 0);
  ring.consume(100);
  assert(ring.empty());
  ring.clear();
  assert(ring.free() == 5);
  assert(ring.push(first, 4) == 4);
}

int main() {
  test_policy();
  test_profile();
  test_mock_driver_interface();
  test_ring_wrap_and_backpressure();
  return 0;
}
