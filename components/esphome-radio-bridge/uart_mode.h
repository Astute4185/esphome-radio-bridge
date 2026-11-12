#pragma once
#include <cstdint>
namespace esphome::esphome_radio_bridge::uart_modes {
enum class UartModeId : uint8_t { TWO_WIRE, FOUR_WIRE };
constexpr bool valid_uart_mode(UartModeId mode) {
  return mode == UartModeId::TWO_WIRE || mode == UartModeId::FOUR_WIRE;
}
constexpr bool requires_rts_cts(UartModeId mode) { return mode == UartModeId::FOUR_WIRE; }
constexpr const char* mode_name(UartModeId mode) {
  return requires_rts_cts(mode) ? "4-wire (RTS/CTS)" : "2-wire (TX/RX)";
}
struct UartPinout {
  uint8_t port;
  int tx;
  int rx;
  int rts;
  int cts;
  uint8_t rts_threshold;
};
class UartHardware {
 public:
  virtual ~UartHardware() = default;
  virtual bool set_pins(int tx, int rx, int rts, int cts) = 0;
  virtual bool set_flow_control(bool enable, uint8_t rts_threshold) = 0;
};
class UartModeStrategy {
 public:
  virtual ~UartModeStrategy() = default;
  virtual UartModeId id() const = 0;
  virtual bool apply(UartHardware& hardware, const UartPinout& pins) const = 0;
};
constexpr bool valid_uart_pinout(const UartPinout& p, UartModeId normal, UartModeId boot) {
  if (!valid_uart_mode(normal) || !valid_uart_mode(boot) || p.port > 2 || p.tx < 0 || p.tx > 33 || p.rx < 0 ||
      p.rx > 39 || p.tx == p.rx)
    return false;
  // A two-wire profile may retain valid, unused RTS/CTS pin mapping for
  // hardware that supports a future four-wire firmware migration.
  if (!requires_rts_cts(normal) && !requires_rts_cts(boot) && p.rts == -1 && p.cts == -1 &&
      p.rts_threshold == 0)
    return true;
  if (p.rts < 0 || p.rts > 33 || p.cts < 0 || p.cts > 39 || p.rts_threshold == 0 || p.rts_threshold > 127)
    return false;
  return p.tx != p.rts && p.tx != p.cts && p.rx != p.rts && p.rx != p.cts && p.rts != p.cts;
}
}  // namespace esphome::esphome_radio_bridge::uart_modes
