# Firmware catalogue integration

`feature/firmware-catalogue` adds on-demand HTTPS metadata resolution, not daily update
checks or flashing. The next branch introduces scheduling and update-available state.

- `catalogue_url` (optional): absolute HTTPS URL overriding the selected board
  driver's default catalogue. With no YAML value, the driver supplies its own
  canonical URL. The TubesZB driver defaults to
  `https://raw.githubusercontent.com/Astute4185/esphome-radio-bridge/main/firmware/catalogue.json`.
  Other drivers may supply different URLs or none at all.
- `manifest_override` (optional ESPHome text entity): absolute HTTPS URL directly to
  an alternative `manifest.json`. Persisted in ESPHome preferences. Clear it to use
  the default catalogue again. Changing it invalidates cached metadata.
- `refresh_catalogue` (optional button): initiate a metadata request, non-blocking
  with respect to the ESPHome main loop. No automatic polling in this branch.
- `catalogue_status` (optional text sensor): Idle, Loading, Loaded version, or Error.

A custom manifest override takes precedence over the driver default and the YAML
`catalogue_url`. Clearing the override returns to the driver's catalogue without
reflashing. Changing the YAML `catalogue_url` still requires an ESP32 firmware update.
If the selected driver has no default and neither override is configured, the
radio bridge remains functional with firmware checks disabled.

The driver supplies `board`, `radio_type`, and its optional default catalogue URL.
The resolver handles catalogue-directory-relative and overlapping repository-root
paths, including `firmware/...` entries when the catalogue is already hosted under
`firmware/`. A `./` prefix explicitly means catalogue-directory-relative. URL
traversal, different hosts and non-HTTPS paths are rejected.

Both catalogue and manifest use `schema_version: 1`. Manifests contain a `latest`
version pointer and a `versions` array. Each release specifies `firmware_version`,
`radio_version`, `release_type` (`beta`, `candidate`, or `release`), a 64-character
`sha256`, and an absolute HTTPS GitHub Release download URL in `file`. No firmware binary is downloaded by
this branch. `latest` must match one of the version entries. Empty manifests use
`latest: null` and `versions: []`.

For ESPHome 2026.10.2, firmware metadata requests run in a FreeRTOS worker
so UART and TCP handling remain responsive. Keep HTTPS certificate verification
enabled. The raw GitHub catalogue and manifests typically do not redirect, but
firmware binaries hosted in GitHub Releases **do**; `follow_redirects: true`
and `buffer_size_tx: 4096` are therefore required by the on-device flasher.
See [firmware flashing](firmware_flashing.md) for the validated HTTP settings.

Security limits: at most 16 KiB per JSON document, 48 versions, and 255 characters
for the persisted override. Schema validation rejects mismatched board/radio and
malformed metadata. A failed request never resets or takes ownership of the radio.
