# Radio firmware flashing: ESP32 staging and HTTP requirements

The bridge stages a complete firmware `.gbl` image in ESP32 flash, validates
its SHA-256 digest, and **only then** sends it to the EFR32 Gecko bootloader.
This prevents a truncated or corrupted download from being used for flashing.
The radio's UART bootloader is independent of the ESP32's own OTA partitions.

## Build-time requirements and deployment

| Requirement | Why | Where it is configured | Existing device deployment |
| --- | --- | --- | --- |
| `radio_fw` (512 KiB) ESP32 data partition | Stores the full verified `.gbl` while flashing | **Automatically registered by the TubesZB driver** when `flash_firmware:` is configured | Separate partition-table upload or serial flash |
| `http_request.buffer_size_tx: 4096` or larger | Long GitHub Release signed redirect URLs exceed ESPHome's 512-byte HTTP TX default (`HTTP_CLIENT: Out of buffer`) | **User YAML**, checked at configuration validation | Ordinary ESP32 firmware OTA |
| `http_request.buffer_size_rx: 1024` | Extra response headroom; 512 can suffice for some endpoints | Recommended in YAML, not mandatory | Ordinary ESP32 firmware OTA |
| `http_request.follow_redirects: true` | Releases redirect to a CDN | YAML, checked at configuration validation | Ordinary ESP32 firmware OTA |
| `http_request.verify_ssl: true` | Verify the HTTPS server's identity | YAML, checked at configuration validation | Ordinary ESP32 firmware OTA |
| `ota.allow_partition_access: true` | Authorises the special ESP32 partition-table upload over native ESPHome OTA | YAML | First install a normal firmware OTA, then perform the partition-table upload |

The driver supplies the partition at **build time**, not on the device at
runtime. Firmware OTA uploads update the application; they do not install the
new partition table. Configuring the partition alone therefore does not make
it appear on an existing ESP32. The partition is registered only for configs
that expose `flash_firmware:`. Firmware monitoring and TCP bridging can work
without a staging partition.

You can still declare `radio_fw` explicitly under `esp32.partitions`; the driver
recognises an existing named partition and does not add a duplicate. With a
custom partition CSV, you must explicitly include a `radio_fw` data partition
with adequate capacity. The staging size is currently fixed at 512 KiB for
TubesZB; `.gbl` files larger than the staging partition are rejected.

## Copyable ESPHome configuration

The full example at `examples/tubeszb-mgm210-poe-2023.yaml` enables all required
settings. Its essential portions are:

```yaml
esp32:
  board: esp-wrover-kit
  framework:
    type: esp-idf
  # No esp32.partitions entry needed with this driver.

ota:
  - platform: esphome
    allow_partition_access: true

http_request:
  verify_ssl: true
  follow_redirects: true
  redirect_limit: 5
  buffer_size_tx: 4096  # Mandatory for flash-enabled configuration.
  buffer_size_rx: 1024
  timeout: 15s

esphome-radio-bridge:
  driver: tubeszb_mgm210_poe_2023
  flash_firmware:
    name: EFR32 Update Firmware
  flash_armed:
    name: EFR32 Firmware Flash Armed
  firmware_update_status:
    name: EFR32 Firmware Update Status
```

The **codebase declares** the staging partition using ESPHome's `esp32.add_partition()`
API, and **rejects** insufficient HTTP settings at YAML validation instead of
letting the flash fail at runtime. The HTTP client remains the shared ESPHome
`http_request` component. Automatically overriding its buffer or TLS settings
could interfere with other consumers; a dedicated downloader/client would be
required to remove these HTTP YAML settings safely.

## Docker: updating the ESP32 partition table

This is a **one-time operation** per flash layout, not something to repeat for
each EFR32 update. A stable power supply and a serial recovery route are
strongly recommended; a failed/interrupted partition migration may require
serial reflashing. Partition changes can also relocate NVS or app slots.

With the example device name and IP shown below, and an ESPHome Docker
container called `esphome` with its YAML mounted at `/config`:

```bash
# 1. Apply the YAML containing allow_partition_access: true and use a normal
#    ESPHome firmware OTA first if the running firmware lacks that permission.
# 2. Compile the full flash-capable YAML (creates the new partition table).
docker exec -it esphome esphome compile /config/tubeszb-efr32-mgm210-poe-2023.yaml

# 3. Explicitly upload the partition table over native ESPHome OTA.
docker exec -it esphome esphome upload \
  /config/tubeszb-efr32-mgm210-poe-2023.yaml \
  --partition-table --device 10.0.9.100

# 4. View the active partition table reported by the device.
docker exec -it esphome esphome logs \
  /config/tubeszb-efr32-mgm210-poe-2023.yaml --device 10.0.9.100
```

In the device logs, **`Partition access allowed` is only permission**, not
proof of the new layout. Find `radio_fw` under `Partition table:` with size
`0x80000`. The partition is not usable until that entry is present on-device.

The ESP32 module currently reports a 4 MiB layout with two OTA app slots;
the staging space reduces available app partition capacity. Verify the new
application fits both slots. ESPHome performs migration checks, but OTA table
updates still carry recovery risk. A serial flash is safer for a physically
accessible board. The LAN8720 wiring uses GPIO16/17; do not assume standard
WROVER PSRAM is available.

## Troubleshooting

| Status/log | Meaning / action |
| --- | --- |
| `Failed: Missing radio_fw staging partition` | New partition was not installed on the ESP32. Check the device's active partition table. Do not just repeat app OTA. |
| `HTTP_CLIENT: Out of buffer` followed by `HTTPS firmware request failed` | Increase `http_request.buffer_size_tx` to at least 4096, compile and normal OTA. |
| `Firmware Content-Length missing or exceeds staging partition` | Manifest's firmware binary is too large, or the server omitted Content-Length. |
| `SHA-256 checksum mismatch` | Download does not match manifest; do not flash. |
| `Failed: ...` with bridge recovering to Ready | UART maintenance shutdown and bridge recovery worked; inspect the specific download or flash error. |

## Firmware controls and safety

The bridge uses an explicit `Firmware Flash Armed` switch (off by default), an
`Update Firmware` button and a single `Firmware Update Status` text sensor.
Daily firmware checks **do not** automatically flash. The TCP listener is
closed during maintenance; stop ZHA before flashing. The Gecko bootloader must
confirm a completed installation: an XMODEM EOT ACK by itself is insufficient.

On-device flashing stages the complete binary, verifies SHA-256, enters the
bootloader, transfers, restores normal radio mode, and reports progress in 5%
steps: `Downloading N%`, `Verifying firmware`, `Verified: entering bootloader`,
`Flashing N%`, and `Restoring radio`, ending in `Completed` or `Failed: ...`.
The final state persists until the next accepted attempt, but not across an
ESP32 reboot. ZHA reconnection and radio identification remain separate
acceptance tests.

See also the official ESPHome documentation for [custom ESP32 partitions](https://esphome.io/components/esp32/),
[HTTP Request](https://esphome.io/components/http_request/) and
[OTA partition-table updates](https://esphome.io/components/ota/esphome/).

## Gecko bootloader handshake and timeouts

The board driver switches the UART to its bootloader configuration (normally
115200 baud, 8N1, with RTS/CTS disabled) and toggles BOOT/RESET. The Gecko
XMODEM protocol then sends carriage return (`\r`) to request the standalone
bootloader menu, retrying every three seconds if the initial request was lost
during radio reset. It waits for `BL >` before sending the ASCII `1` upload
command, and then waits for the bootloader's `C` to begin XMODEM-CRC.

The status sensor distinguishes three different timeouts:

- `Bootloader menu timed out (no BL > prompt)`: check BOOT/RESET entry,
  the bootloader's UART pinout, baud rate and two-wire UART readiness.
- `Bootloader XMODEM start timed out (no C)`: the menu was received and
  the upload command sent, but the bootloader did not start XMODEM.
- `Bootloader completion timed out (no BL > prompt)`: XMODEM transfer
  reached EOT ACK but the bootloader never confirmed installation.

These are bootloader transport diagnostics, not confirmation of the selected
EFR32 application firmware's normal-mode RTS/CTS pin mapping.

## Selecting a firmware release

Enable the optional `firmware_version_select:` entity under `esphome-radio-bridge:`
when the firmware flashing button is present:

```yaml
esphome-radio-bridge:
  driver: tubeszb_mgm210_poe_2023
  manifest_override:
    name: EFR32 Firmware Manifest Override URL
  firmware_version_select:
    name: EFR32 Firmware Version
  firmware_version_status:
    name: EFR32 Firmware Version List Status
  flash_armed:
    name: EFR32 Firmware Flash Armed
  flash_firmware:
    name: EFR32 Update Firmware
```

- **Latest** follows the active manifest's `latest` release ID. All release
  choices show radio stack version, release ID and release type (for example,
  `9.1.2 (release 0.2, beta)`). Release ID is the unique selection key, not
  the stack version; two builds with the same stack version can differ in
  RTS/CTS or other radio settings.
- The dropdown is rebuilt from every successfully validated manifest, whether
  it came from the driver's default catalogue, a custom YAML catalogue, or
  the Home Assistant **Manifest Override URL**.
- On a successful refresh, the last selected release is retained if its
  release ID still exists; otherwise it falls back to **Latest**.
- **NVS cache:** After a successful download, the ESP32 saves the options and
  selected release ID in flash, avoiding repeat writes when nothing changed.
  At the next boot, the options are restored during component setup, *before
  native API entity discovery*. A network outage leaves the cached choices
  visible. Up to 24 entries of at most 95 label bytes each can be cached;
  larger lists still work live but cannot be fully restored after reboot.
- The cache is keyed to the board, firmware channel, catalogue URL and custom
  manifest override. Switching sources invalidates choices from the previous
  source. The cache stores **display labels and release IDs only**, never
  release URLs or checksums. **Flashing requires a fresh, validated manifest**;
  offline/cached selections alone can never start a flash.
- Selecting an entry does not flash the radio. Disconnect ZHA, arm the
  flasher, then use **EFR32 Update Firmware** to install the chosen release.
  Firmware availability monitoring continues to compare against the manifest's
  `latest`, independent of the selected rollback version.

### Home Assistant dropdown refresh behavior

ESPHome 2026.10.2 sends a select's option list during native API entity
enumeration. It does **not** send a dynamic option-list update to an already
connected Home Assistant client. We investigated API client reconnection but
could not verify a stable, supported external-component method for forcing
re-enumeration. **This branch does not disconnect API clients or reboot.**

The NVS cache fixes the *ordinary reboot* race after the first successful
manifest download. A first-ever boot or a newly changed custom manifest can
still add or remove options while Home Assistant is already connected. In that
case, go to **Home Assistant → Settings → Devices & services → ESPHome** and
**Reload** this device's integration entry to refresh the list. The optional
**EFR32 Firmware Version List Status** sensor reports when a manual reload may
be needed. This does not interrupt the independent Zigbee TCP bridge.

The ESPHome web server's select options can also be refreshed by reloading its
browser page. Do not treat a cached list as permission to flash while offline.
