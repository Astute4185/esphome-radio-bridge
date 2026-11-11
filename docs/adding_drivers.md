# Adding another radio/board driver

The ESP32 board and the radio module are **different concerns**. The gateway core
runs on an ESP32 with ESP-IDF and any ESPHome-supported network interface. The
selected radio driver describes the wiring and radio-specific control signals.
The existing driver is named `tubeszb_mgm210_poe_2023`; the same ESP32/PoE board
may be used with other radio modules without duplicating the TCP bridge.

## Contract

A production driver implements `BoardDriver` and provides a static-validated
`BoardProfile` with UART port and pins, normal/bootloader mode IDs, baud rates,
radio reset/bootloader GPIO and polarity, and firmware protocol metadata.

- `UartModeId::TWO_WIRE`: only TX and RX; RTS/CTS pins are -1 if both stages
  use two-wire. No RTS/CTS handshaking.
- `UartModeId::FOUR_WIRE`: TX, RX, RTS, CTS and a valid RX threshold required.
- **The driver selects the UART strategy**, not YAML or the gateway core.
- Concrete drivers own reset/bootloader sequences but do not duplicate UART
  mode implementations.

See [UART modes](uart_modes.md) for strategy examples and transition details.

## Checklist for a new production driver

1. Obtain the actual hardware schematic and verify TX/RX. If either mode
   uses four-wire, confirm RTS/CTS crossings. Never reuse mock GPIO mappings.
2. Create `components/serial_radio_gateway/drivers/<model>_profile.h` with
   the immutable `BoardProfile`, and `static_assert(valid_profile(PROFILE))`.
3. Implement `drivers::BoardDriver` in `drivers/<model>.h`. The existing
   `tubeszb_mgm210_poe_2023.h` illustrates active-low reset/bootloader GPIOs;
   other radio controllers may use completely different sequences.
4. Add the enum value and factory case in `drivers/board_factory.h`.
5. Select the normal and bootloader UART strategies in the profile. Add the
   same driver ID in the Python `BOARD_DRIVERS` tuple in `__init__.py`.
   Keep enum names and the uppercased driver IDs consistent; codegen maps the
   name to `BoardModel`.
6. Add a dedicated example and C++ UART/profile/policy tests. Add config-only
   validation and compile checks to CI for the new driver.
7. If firmware flashing is supported, implement or select a **separate host
   protocol tool**. Never send Gecko XMODEM to a different radio/bootloader.
8. Test hardware reset, bootloader entry, timeout recovery, and TCP reconnect
   with the target radio, including a failed flash and power-cycle recovery.

## Driver selected from YAML

```yaml
serial_radio_gateway:
  driver: tubeszb_mgm210_poe_2023
  tcp_port: 6638
```

There is intentionally **no `uart_mode`, `rts_pin`, `cts_pin`, or
`hardware_flow_control` YAML option**. The driver owns signalling choices.
A two-wire-only driver is supported; the MGM210 driver still requires
four-wire normal operation.

## Same ESP32 network board, different radio

An ESP32/PoE base package may be reused unchanged while the selected radio
changes. For example, to support another physical radio on the same ESP32,
create a driver `my_radio_rev_b` (with verified wiring) and then select it:

```yaml
packages:
  ethernet: !include ../packages/esp32-wrover-lan8720.yaml

serial_radio_gateway:
  driver: my_radio_rev_b  # example ID, NOT implemented yet
  tcp_port: 6638
```

Until that driver is implemented and tested, this configuration will be rejected
by ESPHome config validation. This is intentional: we will not silently select
or synthesize GPIO mappings for unknown boards.

The generic TCP gateway does not probe the firmware or own the flashing format.
Those are explicit future protocol-level extensions, scoped by driver capability.
