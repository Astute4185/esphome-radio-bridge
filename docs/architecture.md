# Architecture and test plan

## Layers

1. **ESPHome YAML** selects a registered board driver. The ESP32/Ethernet
   package is independent of the radio hardware.
2. **Gateway core** owns TCP, lifecycle, maintenance guards and HA entities.
   It neither selects the UART mode nor calls ESP-IDF UART functions.
3. **Board driver** owns pin mapping, reset/bootloader sequencing, baud rates,
   firmware protocol metadata, and UART mode selection for each lifecycle.
4. **UART strategies** are reusable `TwoWireMode` and `FourWireMode` modules,
   independently testable through an injected `UartHardware` interface.
5. **ESP-IDF UART bus adapter** owns the peripheral lifecycle, pin routing,
   data transfer, and dispatches the strategy selected by the board driver.
6. **Firmware downloader/storage** stages the complete HTTPS `.gbl` image in
   the driver's ESP32 partition, verifies SHA-256, and remains independent of
   radio UART bootloader protocols.
7. **Flash protocol** implements Gecko XMODEM-CRC for compatible EFR32 radios;
   host flashing tools remain available for recovery and engineering tests.

## State model

| State | UART mode | Board function | Network |
| --- | --- | --- | --- |
| NORMAL | Driver's normal mode | Application reset/baud | One TCP client |
| BOOTLOADER | Driver's boot mode | Boot/reset/baud | Flash client |
| RECOVERY | Restore normal mode | Bootloader exit | Disconnect stale client |

MGM210 PoE 2023 uses **four-wire → two-wire → four-wire**, with hardware
RTS/CTS in normal NCP mode and two-wire Gecko bootloader access. Other verified drivers
can use two-wire-only, four-wire-only, or mixed stage profiles.

A failed bootloader transition attempts recovery. Mode restoration is always
re-applied even if normal and bootloader baud rates are equal.

The ESP32 side owns control of GPIO reset/bootloader pins; external flash hosts
use ESPHome/HA API for mode changes. All bootloader transitions require arming
and a disconnected TCP client. The guard is intentionally not exposed as an
arbitrary UART flow-control toggle.

## Test coverage

| Coverage | Mechanism |
| --- | --- |
| Board profile validity and overlapping GPIOs | Native C++ static assertions |
| MGM210 2023 physical mapping | Native C++ profile test + Python source contract |
| Mock two-wire, four-wire and hybrid board profiles | Native C++ tests |
| State machine permission policy | Native C++ tests |
| UART strategy pin routing, transition order and failures | Native fake-hardware C++ tests |
| UART buffering and wraparound | Native C++ tests |
| XMODEM CRC, retry, failure and completion | Python unit tests with fake bootloader |
| Manifest validation | Python unit tests |
| ESPHome config and generated firmware | `tests/components/esphome-radio-bridge/test.esp32-idf.yaml` and `scripts/validate.sh` in CI |
| Firmware-staging build requirements | Host Python tests for driver partition registration and HTTP validation |
| Live reset, flow control, flashing and ZHA reconnect | Hardware acceptance tests required |

## Known limitations

- Only the MGM210 2023 driver is production-selectable; additional profiles
  are native test fixtures.
- Radio identification and on-device firmware download are implemented;
  end-to-end bootloader flashing still requires hardware acceptance testing.
- The `radio_fw` staging partition is declared at build time when flashing is
  enabled; existing ESP32 devices need a distinct partition-table installation.
- The generic gateway requires ESP32 with ESP-IDF and a network interface.
  It is not a generic USB-to-UART serial integration or a Thread border router.
- TCP port 6638 is a raw, unauthenticated serial endpoint: restrict network
  access. ESPHome API button controls must also be protected.
- MGM210 normal-mode four-wire communication requires a matching EFR32 NCP
  image with CTS routed to PD02 and RTS routed to PC01; a two-wire-only NCP
  image will not communicate with the production driver configuration.
