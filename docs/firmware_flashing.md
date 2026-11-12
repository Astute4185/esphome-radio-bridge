# Radio firmware flashing

The generic `RadioBridge` owns UART arbitration and update orchestration.
`FlashProtocol` is the chipset-independent transfer contract selected by the
board driver (`GeckoXmodem` on TubesZB MGM210). A separate `FirmwareStorage`
contract lets the downloader stage a complete image in a dedicated data partition.
No PSRAM dependency is introduced.

Firmware bytes come exclusively from the selected board-compatible manifest's
`latest` GitHub Release URL. A worker downloads the complete file to the
`radio_fw` data partition, validates SHA-256, and only then enters bootloader.

## Home Assistant controls

- `Firmware Maintenance Armed`: must be on for explicit flashing (default off)
- `EFR32 Update Firmware`: begins the selected firmware installation
- `EFR32 Firmware Update Status`: text sensor for download/flash percentage,
  completed or failure reason

No automatic flashing occurs during daily update checks. The TCP listener is
held closed during an explicitly requested update. A live ZHA client blocks
starting the update. The serial transfer requires the Gecko bootloader's final
successful-installation message; XMODEM EOT ACK alone is not sufficient.

## Staging partition and upgrading deployed ESP32 devices

Explicitly add the optional `packages/radio-flash-staging.yaml` to an ESPHome
configuration when you are ready to use this feature. The normal examples leave
this package commented out to avoid changing an already-deployed partition table. This reserves a dedicated 512 KiB flash scratch area for the
radio GBL. The image size must not exceed this partition. Larger firmware
images require a larger scratch area and adequate app partition space.

**CRITICAL:** Changing the partition table requires a USB/serial ESP32 flash.
Do not introduce the staging partition to an already-deployed device via a
routine OTA update: the ESP32 partition table is not updated by app-only OTA.
Verify the resulting ESP32 app partition sizes before flashing.

The GPIO16/17 functions used by this TubesZB design rule out assuming the
external PSRAM configuration of a standard ESP32-WROVER module; no PSRAM
component is enabled by this patch.

The HTTPS component must use `verify_ssl: true` and `follow_redirects: true`
for GitHub Release binary downloads. A failure to restore UART application
mode is fatal and requires manual recovery.

## Unified update diagnostics

`EFR32 Firmware Update Status` is the only flash progress/result entity. Its
state transitions through `Downloading N%`, `Verifying firmware`,
`Verified: entering bootloader`, `Flashing N%`, `Restoring radio`, and either
`Completed` or `Failed: <reason>`. Percentages update in 5% increments.

A rejected update request reports `Blocked: <reason>` only when no previous
terminal outcome is displayed. `Completed` and `Failed: ...` remain visible
until an accepted new update attempt starts. Reasons distinguish missing
maintenance authorisation, an active TCP client, network loss, unavailable
firmware metadata, and transfer or radio recovery errors.

The terminal `Completed` status means the bootloader confirmed installation
and normal UART mode was restored; ZHA reconnection and subsequent radio
identification are separate hardware validation gates. This status is not
persisted across ESP32 restarts.
