# ESPHome Serial Radio Gateway

Reusable, board-driver-based external component for network-attached serial
radio modules on ESP32/ESP-IDF. The board driver selects **two-wire (TX/RX)**
or **four-wire (TX/RX/RTS/CTS)** independently for normal and bootloader modes.
Reusable UART mode strategies implement flow control and pin routing; the
board driver provides verified physical GPIO assignments, radio reset/boot
sequences, baud rates, and firmware protocol metadata. ESP32 networking, TCP
bridging, safety policy and Home Assistant entities remain generic.

This project is a refactor of an MGM210-specific prototype. Its first supported
production driver is `tubeszb_mgm210_poe_2023` (TubesZB MGM210 PoE 2023). No
other physical pin mapping is claimed to be tested or production-ready.

## Quick start

```yaml
esphome:
  name: zigbee-gateway
  min_version: 2026.10.1

packages:
  baseboard: !include packages/esp32-wrover-lan8720.yaml

external_components:
  - source:
      type: local
      path: components
    components: [serial_radio_gateway]

logger:
  baud_rate: 0
api:
  encryption:
    key: !secret apikey_tubeszb
ota:
  - platform: esphome

serial_radio_gateway:
  driver: tubeszb_mgm210_poe_2023
  tcp_port: 6638
  connected:
    name: Radio TCP Client Connected
  flash_armed:
    name: Radio Firmware Flash Armed
  radio_reset:
    name: Radio Reset
  enter_bootloader:
    name: Enter Radio Bootloader
  exit_bootloader:
    name: Exit Radio Bootloader
  mode:
    name: Radio Mode
```

Relative include paths in this snippet assume the configuration file is at the
repository root. For copyable working files located in `examples/`, see
`examples/tubeszb-minimal.yaml` or `examples/tubeszb-mgm210-poe-2023.yaml`.
These import the same reusable LAN8720 baseboard package.

**Prerequisites:** Confirm GPIO2/13 represent the actual ESP32 RTS/CTS pins on
your device. The MGM210 driver mandates **four-wire normal mode** and **two-wire Gecko
bootloader mode**. Its NCP firmware MUST support hardware RTS/CTS. Your earlier
two-wire NCP image is not compatible with that MGM210 driver configuration. Preserve a physical recovery route before deployment.

## How to reuse it for other modules

Change only the YAML `driver:` value after adding a new verified driver module.
The core does not change. New drivers implement the `BoardDriver` C++ interface
and register their enum/factory and Python schema ID. The Ethernet/ESP32 package
can be reused for multiple radios attached to the same ESP32 board design.

See [Adding a driver](docs/adding_drivers.md),
[UART modes](docs/uart_modes.md), and [Architecture](docs/architecture.md).

### Repository layout

```text
components/serial_radio_gateway/
  __init__.py                 # ESPHome config schema and code generation
  serial_radio_gateway.h/.cpp # TCP bridge, lifecycle, HA entities
  byte_ring.h                 # bounded transport buffers
  mode_policy.h               # host-testable transition rules
  uart_modes/
    uart_mode.h                # mode contract, pin validation, test interface
    two_wire.h                 # TX/RX signalling strategy
    four_wire.h                # TX/RX/RTS/CTS signalling strategy
    mode_factory.h             # reusable mode selection
    esp_idf_uart_bus.h         # ESP-IDF peripheral adapter
  drivers/
    board_driver.h           # generic board abstraction and pin validation
    board_factory.h          # production driver registry
    tubeszb_mgm210_poe_2023_profile.h # pure host-testable board definition
    tubeszb_mgm210_poe_2023.h # ESP-IDF GPIO-specific radio control
packages/
  esp32-wrover-lan8720.yaml   # shared ESP32/Ethernet hardware
examples/
  tubeszb-minimal.yaml
  tubeszb-mgm210-poe-2023.yaml
scripts/
  flash_gecko.py              # Gecko-specific *host* XMODEM-CRC tool
  lint.sh
  test.sh
  validate.sh
tests/components/             # ESPHome component compile fixture
tests/cpp/                    # host C++ policy/buffer/profile + UART strategy tests
tests/python/                 # host Python manifest/XMODEM/contracts tests
docs/
  adding_drivers.md
  architecture.md
.github/workflows/ci.yaml     # lint, tests, ESPHome validation/build
```

### UART modes and ownership

| Radio lifecycle | Selected mode | Responsibility |
| --- | --- | --- |
| Normal application | Per driver: 2-wire or 4-wire | UART mode strategy |
| Bootloader | Independently selected by driver | UART mode strategy |
| Reset and bootloader sequencing | Radio-specific | Board driver |
| TCP streaming and maintenance permissions | Independent of UART wiring | Gateway core |
| GBL XMODEM flashing | Gecko-specific | Host flasher |

For `tubeszb_mgm210_poe_2023`, **normal = 4-wire RTS/CTS** and **Gecko
bootloader = 2-wire TX/RX**. A second radio can specify normal = 2-wire and
bootloader = 2-wire, without wiring RTS/CTS at all. Drivers supporting 4-wire
in either stage must provide valid RTS/CTS pins. Modes cannot be overridden
from user YAML, avoiding hardware-specific configuration leaks.

The ESP-IDF adapter retains physical UART signal assignments across a
4-wire-to-2-wire transition; it disables hardware handshaking in two-wire
mode. See [UART modes](docs/uart_modes.md) for the signalling caveat.

There are no user-facing flow-control toggles or direct GPIO switches. Only
one TCP client owns the serial port at a time. Flashing requires the
coordinator integration to be stopped and maintenance armed.

## Development

Python 3.11+ plus C++17 compiler, `clang-format` and ESPHome 2026.10.1 for
validation and firmware builds:

```bash
python -m pip install -e '.[dev]'
./scripts/lint.sh       # Ruff, yamllint, clang-format, shell syntax
./scripts/test.sh       # pytest + native C++ tests
python -m pip install esphome==2026.10.1
./scripts/validate.sh   # ESPHome config for both supplied examples
COMPILE=1 ./scripts/validate.sh  # also compile the full ESP32 firmware
```

GitHub Actions runs the same checks and a build. `examples/secrets.yaml` is
created by `validate.sh` using obviously non-production placeholders if absent.
Never use the supplied example secrets on an internet-exposed device.

## Host Gecko flashing (only MGM/Gecko drivers)

```bash
python scripts/flash_gecko.py --help
```

The host tool validates a firmware file or HTTPS manifest and drives Gecko
XMODEM-CRC over the TCP UART, using protected Home Assistant services to enter
and exit maintenance. It does NOT download or flash firmware autonomously from
ESPHome. Confirm the image targets the actual radio hardware and contains
hardware-flow-enabled application firmware. Do not use this tool for other radio
bootloaders just because they connect over the same TCP bridge.

## Implementation and validation boundaries

- Python and native C++ tests exercise policies, buffered transport, two-wire/four-wire
  strategies, board profile variations and Gecko protocol simulation.
- Full ESPHome code generation, ESP-IDF compilation and physical UART/bootloader
  acceptance tests must pass before deploying to your live ZHA coordinator.
- Automated version identification, on-device manifest downloads, progress
  entities and radio-type autodetection are **not yet implemented**.
- Port 6638 is raw TCP UART. It has no independent authentication. Firewall or
  isolate the gateway to a trusted network segment; restrict HA maintenance
  button access and do not expose the web interface without authentication.
- Keep a recoverable firmware/bootloader route available during development.

See [Architecture](docs/architecture.md) for the acceptance-test matrix.
