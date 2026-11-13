# UART signalling strategies

The board driver selects a reusable UART strategy for **normal** and
**bootloader** stages independently. ESPHome YAML selects a board driver, not
low-level UART handshaking. UART mode is not tied to the ESP32 Ethernet baseboard.

| Mode | Hardware signals | Implementation |
| --- | --- | --- |
| `TWO_WIRE` | TX, RX | `two_wire.h` |
| `FOUR_WIRE` | TX, RX, RTS, CTS | `four_wire.h` |

`uart_mode.h` defines the host-testable strategy and pin contracts;
`mode_factory.h` chooses an implementation;
`esp_idf_uart_bus.h` owns the single ESP-IDF UART peripheral.
The common gateway does not call `uart_set_pin` or `uart_set_hw_flow_ctrl`.

## Board driver contract

Every `BoardProfile` supplies `uart`, `normal_uart_mode`,
`bootloader_uart_mode`, `normal_baud`, and `bootloader_baud`. Example:

```cpp
// TubeZB MGM210 PoE 2023 profile, registered in the repository.
{1, 4, 36, 2, 13, 122};  // UART1, TX, RX, RTS, CTS, RX threshold
normal_uart_mode = UartModeId::TWO_WIRE;  // Existing NCP firmware POC
bootloader_uart_mode = UartModeId::TWO_WIRE;  // Gecko bootloader
// Later migration: normal_uart_mode = UartModeId::FOUR_WIRE;

// An illustrative two-wire-only profile; NOT a selectable driver.
{2, 17, 16, -1, -1, 0};
normal_uart_mode = UartModeId::TWO_WIRE;
bootloader_uart_mode = UartModeId::TWO_WIRE;
```

If **either** stage requires four-wire, RTS/CTS pins and an RX threshold must
be supplied and validated. A two-wire-only profile can use RTS=CTS=-1 and
threshold=0, or retain a fully valid but unused RTS/CTS pin mapping for future
migration. `static_assert(valid_profile(PROFILE))` rejects pin overlap and
incomplete handshake GPIO definitions.

## Runtime transition

The gateway verifies the maintenance guard, asks the UART adapter to apply
the selected mode, then invokes the board-specific bootloader/reset sequence.
On exit and recovery, it reapplies the normal mode. Strategy application is
**never skipped because the baud rate is unchanged**.

A two-wire mode disables hardware flow control. In ESP-IDF,
`UART_PIN_NO_CHANGE` preserves any existing GPIO matrix routes; the UART
adapter therefore also resets candidate RTS/CTS GPIO pads to input mode when
returning to two-wire. This cannot remove physical PCB connections. Verify
compatibility with each radio bootloader. ESP-IDF UART signal behavior is documented in the
[ESP-IDF UART reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/uart.html).

## Experimental runtime mode switching

For the TubesZB MGM210 2023 spike, the optional `uart_mode_select` entity permits
switching the **normal-mode** UART between two-wire and two candidate four-wire
assignments without another ESP32 OTA. The **bootloader is always two-wire**.
This is intended for controlled laboratory validation, not automatic UART
detection or permanent production configuration. See
[`uart-mode-spike.md`](uart-mode-spike.md) for the validation plan.

The selector is non-persistent: an ESP32 reboot returns to the board profile's
working two-wire baseline. Its three values are `2-wire (TX/RX)`,
`4-wire (profile RTS/CTS)`, and `4-wire (swapped RTS/CTS)`. The selected
normal-mode wiring is also reported by the existing `mode` text sensor.

**Do not enable four-wire blindly.** The selected ESP32 RTS pad becomes a
driven output. If it is electrically wired to the radio RTS pad (also an
output), the two outputs may contend. Confirm actual pin direction on the
board before setting `flow_control_wiring_verified: true` in YAML.

The selector blocks changes during TCP connection, identification, bootloader,
maintenance arming, and firmware flashing. On a valid mode change it resets
the EFR32 and re-runs radio identification before reopening TCP. Failed UART
configuration is rolled back; unverified pin mappings are rejected.

When switching back to two-wire, the ESP-IDF adapter explicitly resets the
candidate RTS/CTS GPIO pads; simply passing `UART_PIN_NO_CHANGE` does **not**
detach previously configured UART GPIO matrix routes.
