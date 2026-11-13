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
    components: [esphome-radio-bridge]

logger:
  baud_rate: 0
api:
  encryption:
    key: !secret apikey_tubeszb
ota:
  - platform: esphome

esphome-radio-bridge:
  driver: tubeszb_mgm210_poe_2023
  tcp_port: 6638
  bridge_ready:
    name: Radio Bridge Ready
  bridge_status:
    name: Radio Bridge Status
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

**TubesZB four-wire default:** The MGM210PA32JIA driver now uses **hardware
RTS/CTS for its normal NCP application**, with ESP32 GPIO2 RTS connected to
EFR32 PD02 CTS and EFR32 PC01 RTS connected to ESP32 GPIO13 CTS. The installed
EFR32 firmware must enable that same routing. The Gecko bootloader retains
**two-wire TX/RX** for firmware recovery. Existing two-wire-only NCP images
are incompatible with the normal-mode configuration. Preserve a physical
recovery route before deploying.

## EFR32 firmware flashing prerequisites

The **full example** now includes the HTTP settings proven on hardware:

```yaml
http_request:
  verify_ssl: true
  follow_redirects: true
  redirect_limit: 5
  buffer_size_tx: 4096  # GitHub Release redirects exceed the 512-byte default.
  buffer_size_rx: 1024
  timeout: 15s

ota:
  - platform: esphome
    allow_partition_access: true
```

When `flash_firmware:` is configured, the TubesZB driver **automatically adds**
the 512 KiB `radio_fw` ESP32 staging partition to the generated build. It stores
and verifies the entire `.gbl` before changing the radio firmware. Normal
firmware-monitoring-only configs don't need this partition. The HTTP transmit
buffer remains an explicit `http_request` setting and is validated at build
time for flash-enabled configs; the shared ESPHome HTTP client isn't silently
reconfigured by the bridge.

**A normal application OTA does not update a previously installed ESP32
partition table.** For existing ESP32 devices, separately upload the new
partition table via ESPHome CLI (`esphome upload --partition-table`) with
`ota.allow_partition_access: true`, or flash via USB/serial. See
[firmware flashing and Docker instructions](docs/firmware_flashing.md) for
commands and recovery risks.

## How to reuse it for other modules

Change only the YAML `driver:` value after adding a new verified driver module.
The core does not change. New drivers implement the `BoardDriver` C++ interface
and register their enum/factory and Python schema ID. The Ethernet/ESP32 package
can be reused for multiple radios attached to the same ESP32 board design.

See [Adding a driver](docs/adding_drivers.md),
[UART modes](docs/uart_modes.md), and [Architecture](docs/architecture.md).

### Repository layout

```text
components/esphome-radio-bridge/
  __init__.py                 # ESPHome config schema and code generation
  esphome_radio_bridge.h/.cpp # TCP bridge, lifecycle, HA entities
  bridge_health.h             # host-tested bridge readiness and failure classification
  byte_ring.h                 # bounded transport buffers
  mode_policy.h               # host-testable transition rules
  uart_mode.h               # mode contract, pin validation and test interface
  two_wire.h                # TX/RX signalling strategy
  four_wire.h               # TX/RX/RTS/CTS signalling strategy
  mode_factory.h            # reusable mode selection
  esp_idf_uart_bus.h        # ESP-IDF peripheral adapter
  board_driver.h            # board abstraction and pin validation
  board_factory.h           # production driver registry
  tubeszb_mgm210_poe_2023_profile.h # host-testable board definition
  tubeszb_mgm210_poe_2023.h # ESP-IDF GPIO-specific control
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
| GBL XMODEM flashing | Gecko-specific | ESP32 flash protocol or host recovery tool |

For `tubeszb_mgm210_poe_2023`, **normal = 4-wire RTS/CTS** and **Gecko
bootloader = 2-wire TX/RX**. Other drivers can choose two-wire-only signalling
if their hardware requires it. Drivers supporting 4-wire
in either stage must provide valid RTS/CTS pins. Modes cannot be overridden
from user YAML, avoiding hardware-specific configuration leaks.

The ESP-IDF adapter retains physical UART signal assignments across a
4-wire-to-2-wire transition; it disables hardware handshaking in two-wire
mode. See [UART modes](docs/uart_modes.md) for the signalling caveat.

The confirmed TubesZB PoE 2023 connections are **ESP32 GPIO2 RTS -> EFR32
PD02 CTS** and **EFR32 PC01 RTS -> ESP32 GPIO13 CTS**. An NCP image built with
EFR32 RTS assigned to PC02 will not use the required physical signal path.

There are no user-facing flow-control toggles or direct GPIO switches. Only
one TCP client owns the serial port at a time. Flashing requires the
coordinator integration to be stopped and maintenance armed.

## Development

Python 3.11+ plus C++17 compiler, `clang-format` and ESPHome 2026.10.2 for
validation and firmware builds:

```bash
python -m pip install -e '.[dev]'
./scripts/lint.sh       # Ruff, yamllint, clang-format, shell syntax
./scripts/test.sh       # pytest + native C++ tests
python -m pip install esphome==2026.10.2
./scripts/validate.sh   # ESPHome config for both supplied examples
./scripts/compile.sh    # full ESP-IDF firmware compilation
```

GitHub Actions runs the same checks and a build. `examples/secrets.yaml` is
created by `validate.sh` using a randomly generated API key if absent.
Never use the supplied example secrets on an internet-exposed device.

## Bridge health monitoring

The bridge exposes optional diagnostic entities through the same component YAML:

| Option | HA entity | Meaning |
| --- | --- | --- |
| `bridge_ready` | Binary sensor | UART ready, Ethernet connected, TCP listener running and application mode active |
| `bridge_status` | Text sensor | Starting, Ready, Network offline, TCP listener unavailable, Maintenance, Failed or Stopped |
| `connected` | Binary sensor | A TCP client is connected (not confirmation of Zigbee protocol health) |
| `disconnects` | Counter sensor | Every closed accepted TCP client session, including planned shutdowns |
| `last_disconnect_reason` | Text sensor | Remote closed, TCP socket error, Network lost, Maintenance timeout or ESP32 shutdown |
| `transport_errors` | Counter sensor | Listener failures, network loss affecting an active TCP client, and socket errors |

`bridge_ready` deliberately reports **OFF during bootloader maintenance** and
**ON even with no TCP client** when the network service is listening. This is
transport health, not a Zigbee coordinator heartbeat. A remote TCP close may be
a planned ZHA restart, so it is not counted as a transport error. For HA alerts,
exclude intentional maintenance and use a persistence delay for disconnections.

Failed TCP listener creation/binding is retried every five seconds without
blocking the ESPHome event loop. It does not permanently fail the component.
Counters reset after ESP32 reboot. Additional firmware-probing support is
planned for a later feature branch.

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
- Radio version identification, manifest loading, periodic update checks and
  on-device flashing now have code and host tests; end-to-end flashing still
  requires hardware acceptance testing on each supported board/firmware image.
- Port 6638 is raw TCP UART. It has no independent authentication. Firewall or
  isolate the gateway to a trusted network segment; restrict HA maintenance
  button access and do not expose the web interface without authentication.
- Keep a recoverable firmware/bootloader route available during development.

See [Architecture](docs/architecture.md) for the acceptance-test matrix.

### Driver-owned radio identification

When the UART is initialised in normal application mode, the bridge reserves
exclusive access while the selected driver attempts identification. The TCP
listener becomes available only after the probe finishes or times out. The
TubesZB MGM210 driver uses ASH/EZSP; future drivers may implement other
protocols or report unsupported. Probe failure is diagnostic and does not
block TCP bridging.

Entities: `EFR32 Installed Firmware` (text sensor), `EFR32 Identification
Status` (text sensor) and `EFR32 Probe Firmware` (button). Manual probes are
blocked while a TCP client is connected or maintenance is active. The NCP is
reset after probing so ZHA always starts with a clean radio session.

This feature requires hardware verification against the target MGM210
firmware, especially with four-wire RTS/CTS.

### Firmware release selection

The optional `firmware_version_select:` entity provides a runtime **Latest** or
explicit release choice for EFR32 flashing. The choices are rebuilt from the
validated driver catalogue manifest or a custom manifest override. NVS caching
restores the last known list before Home Assistant reconnects on reboot. Versions
with the same radio stack number remain distinct through their release IDs.
See [firmware flashing documentation](docs/firmware_flashing.md#selecting-a-firmware-release)
for cache boundaries, safety rules and the manual HA integration reload
required when the manifest options change during an active API connection.
