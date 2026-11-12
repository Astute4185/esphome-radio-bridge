# Firmware update monitoring

The `feature/firmware-update-monitoring` branch builds on the catalogue manager.
It downloads **metadata only**. Firmware download and Gecko XMODEM flashing are
separate future work.

## ESPHome configuration

Add these optional entries under `esphome-radio-bridge` (along with the existing
`catalogue_url`, `catalogue_status` and `manifest_override`):

```yaml
  check_updates:
    name: EFR32 Check for Updates
  latest_firmware:
    name: EFR32 Latest Firmware
  update_available:
    name: EFR32 Update Available
```

The existing `refresh_catalogue` button continues to work. Both buttons perform
the same **manual metadata refresh**, without touching the Zigbee UART.

## Schedule

- At startup, check once as soon as network connectivity is available.
- After a successful check, check again after 24 hours (measured from completion).
- After a failed request, retry after 15 minutes while the network is available.
- A manual check is permitted at any time when a request is not already running.
- Setting or clearing the custom HTTPS manifest override invalidates cached
  metadata and triggers a check against the new source when online.
- The ESPHome main loop remains responsive; the existing catalogue worker
  performs HTTPS requests separately from UART and TCP handling.

## Update availability and limitations

The existing driver probe returns the **radio stack version** (such as
`9.1.2.0 (build 123)`), while the release manifest has an independent
`firmware_version` (such as `0.1`). These values must **not** be compared to
each other.

`Update Available` is:

- **ON** when the latest manifest release has a newer radio stack, or when a
  uniquely matched installed release is older than `latest`.
- **OFF** when the radio stack is newer than the published version or a unique
  matching release is already `latest`. Also OFF for a valid empty manifest.
- **UNKNOWN** before metadata loads, after a failed request, when the radio
  version is unavailable, or when multiple releases share an identical radio
  stack version. The bridge invalidates rather than publishing a false OFF.

Matching a radio version to a unique release is an inference, not cryptographic
proof of the installed package. Future firmware formats could expose a separate
installed release identifier to resolve ambiguity.

`latest_firmware` shows the selected manifest `latest` release version,
not the EFR32 stack version. The manifest's `latest` pointer controls which
release is eligible, including a beta or candidate if the publisher selects it.

An automatic check **never flashes or resets the radio**. Firmware installation
will remain an explicit user action in the flashing branch.
