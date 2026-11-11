# UART signalling strategies

The board driver selects a reusable UART strategy for **normal** and
**bootloader** stages independently. ESPHome YAML selects a board driver, not
low-level UART handshaking. UART mode is not tied to the ESP32 Ethernet baseboard.

| Mode | Hardware signals | Implementation |
| --- | --- | --- |
| `TWO_WIRE` | TX, RX | `uart_modes/two_wire.h` |
| `FOUR_WIRE` | TX, RX, RTS, CTS | `uart_modes/four_wire.h` |

`uart_modes/uart_mode.h` defines the host-testable strategy and pin contracts;
`uart_modes/mode_factory.h` chooses an implementation;
`uart_modes/esp_idf_uart_bus.h` owns the single ESP-IDF UART peripheral.
The common gateway does not call `uart_set_pin` or `uart_set_hw_flow_ctrl`.

## Board driver contract

Every `BoardProfile` supplies `uart`, `normal_uart_mode`,
`bootloader_uart_mode`, `normal_baud`, and `bootloader_baud`. Example:

```cpp
// TubeZB MGM210 PoE 2023 profile, registered in the repository.
{1, 4, 36, 2, 13, 122};  // UART1, TX, RX, RTS, CTS, RX threshold
normal_uart_mode = UartModeId::FOUR_WIRE;
bootloader_uart_mode = UartModeId::TWO_WIRE;

// An illustrative two-wire-only profile; NOT a selectable driver.
{2, 17, 16, -1, -1, 0};
normal_uart_mode = UartModeId::TWO_WIRE;
bootloader_uart_mode = UartModeId::TWO_WIRE;
```

If **either** stage requires four-wire, RTS/CTS pins and an RX threshold must
be supplied and validated. A two-wire-only profile uses RTS=CTS=-1 and
threshold=0. `static_assert(valid_profile(PROFILE))` rejects pin overlap and
missing handshake GPIOs.

## Runtime transition

The gateway verifies the maintenance guard, asks the UART adapter to apply
the selected mode, then invokes the board-specific bootloader/reset sequence.
On exit and recovery, it reapplies the normal mode. Strategy application is
**never skipped because the baud rate is unchanged**.

A two-wire mode disables hardware flow control. When switching from four-wire
on the same UART, `UART_PIN_NO_CHANGE` keeps the existing GPIO matrix routes;
this does not physically remove connected RTS/CTS wires. Verify compatibility
with each radio bootloader. ESP-IDF UART signal behavior is documented in the
[ESP-IDF UART reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/uart.html).
