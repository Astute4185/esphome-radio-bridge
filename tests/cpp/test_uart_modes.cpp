#include <cassert>
#include <string>
#include <vector>

#include "mode_factory.h"
#include "tubeszb_mgm210_poe_2023_profile.h"
using namespace esphome::esphome_radio_bridge::uart_modes;
class FakeUart : public UartHardware {
 public:
  bool set_pins(int tx, int rx, int rts, int cts) override {
    calls.push_back("pins:" + std::to_string(tx) + "," + std::to_string(rx) + "," + std::to_string(rts) +
                    "," + std::to_string(cts));
    return !fail_pins;
  }
  bool set_flow_control(bool enable, uint8_t threshold) override {
    calls.push_back(std::string(enable ? "flow:on:" : "flow:off:") + std::to_string(threshold));
    return !fail_flow;
  }
  std::vector<std::string> calls{};
  bool fail_pins{false};
  bool fail_flow{false};
};
int main() {
  constexpr UartPinout two{2, 17, 16, -1, -1, 0};
  static_assert(valid_uart_pinout(two, UartModeId::TWO_WIRE, UartModeId::TWO_WIRE));
  static_assert(!valid_uart_pinout(two, UartModeId::FOUR_WIRE, UartModeId::TWO_WIRE));
  constexpr UartPinout wired_but_disabled{1, 4, 36, 2, 13, 122};
  static_assert(valid_uart_pinout(wired_but_disabled, UartModeId::TWO_WIRE, UartModeId::TWO_WIRE));
  static_assert(!valid_uart_pinout({1, 4, 36, 2, -1, 122}, UartModeId::TWO_WIRE, UartModeId::TWO_WIRE));
  static_assert(!valid_uart_mode(static_cast<UartModeId>(255)));
  FakeUart hw;
  assert(mode_strategy(UartModeId::TWO_WIRE).apply(hw, two));
  assert((hw.calls == std::vector<std::string>{"flow:off:0", "pins:17,16,-1,-1"}));
  constexpr auto mgm = esphome::esphome_radio_bridge::drivers::TUBESZB_MGM210_POE_2023;
  hw.calls.clear();
  assert(mode_strategy(mgm.normal_uart_mode).apply(hw, mgm.uart));
  assert(mode_strategy(mgm.bootloader_uart_mode).apply(hw, mgm.uart));
  // Production profile: four-wire NCP -> two-wire Gecko bootloader -> four-wire NCP.
  // Mode transitions must reapply pin routing and handshaking, even at the same baud.
  static_assert(mgm.normal_baud == mgm.bootloader_baud);
  assert(mode_strategy(mgm.normal_uart_mode).apply(hw, mgm.uart));
  assert((hw.calls == std::vector<std::string>{"flow:off:0", "pins:4,36,2,13", "flow:on:122", "flow:off:0",
                                               "pins:4,36,-1,-1", "flow:off:0", "pins:4,36,2,13",
                                               "flow:on:122"}));
  // Verify direct four-wire mode application and failure paths separately.
  hw.calls.clear();
  assert(mode_strategy(UartModeId::FOUR_WIRE).apply(hw, mgm.uart));
  assert((hw.calls == std::vector<std::string>{"flow:off:0", "pins:4,36,2,13", "flow:on:122"}));
  hw.calls.clear();
  hw.fail_pins = true;
  assert(!mode_strategy(UartModeId::FOUR_WIRE).apply(hw, mgm.uart));
  assert(hw.calls.size() == 2);
  hw.calls.clear();
  hw.fail_pins = false;
  hw.fail_flow = true;
  assert(!mode_strategy(UartModeId::TWO_WIRE).apply(hw, mgm.uart));
  assert(hw.calls.size() == 1);
  hw.calls.clear();
  hw.fail_flow = false;
  assert(!mode_strategy(UartModeId::FOUR_WIRE).apply(hw, two));
  assert(hw.calls.empty());
}
