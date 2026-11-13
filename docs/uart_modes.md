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

### TubesZB MGM210 PoE (2023): verified hardware mapping

On this board, silkscreen/physical module pad labels must not be substituted
for UART signal directions. Confirmed continuity measurements:

| ESP32 UART1 | GPIO / ESP32 module pad | MGM210PA32JIA signal / module pad |
| --- | --- | --- |
| TX (output) | GPIO4 | PA05 RX (input) |
| RX (input) | GPIO36 | PA06 TX (output) |
| RTS (output) | GPIO2 / pad 24 | PD02 CTS (input) / pad 17 |
| CTS (input) | GPIO13 / pad 16 | PC01 RTS (output) / pad 22 |

The Zigbee NCP image must enable USART0 hardware CTS/RTS with
`SL_IOSTREAM_USART_VCOM_CTS_PORT=SL_GPIO_PORT_D`, `CTS_PIN=2`,
`SL_IOSTREAM_USART_VCOM_RTS_PORT=SL_GPIO_PORT_C`, and `RTS_PIN=1`.
An older experiment incorrectly set RTS to PC02. Rebuilding the ESP32
application does **not** correct the EFR32 firmware's pin assignments.

The Gecko serial bootloader stays at **115200 baud, two-wire TX/RX** so
recovery and subsequent GBL uploads do not depend on application RTS/CTS.
The driver sets normal mode to `FOUR_WIRE` and bootloader mode to
`TWO_WIRE`; the experimental runtime selector is not part of the
production driver.

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
