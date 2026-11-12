# Firmware catalogue integration

`feature/firmware-catalogue` adds on-demand HTTPS metadata resolution, not daily update
checks or flashing. The next branch introduces scheduling and update-available state.

- `catalogue_url` (optional): absolute HTTPS URL to `catalogue.json` hosted in the
  separate firmware repository. The example uses the intended production repository;
  configure it to match your published URL.
- `manifest_override` (optional ESPHome text entity): absolute HTTPS URL directly to
  an alternative `manifest.json`. Persisted in ESPHome preferences. Clear it to use
  the default catalogue again. Changing it invalidates cached metadata.
- `refresh_catalogue` (optional button): initiate a metadata request, non-blocking
  with respect to the ESPHome main loop. No automatic polling in this branch.
- `catalogue_status` (optional text sensor): Idle, Loading, Loaded version, or Error.

A custom manifest override can be used without configuring a catalogue URL.
Without either URL, the radio bridge remains functional and the catalogue is disabled.

The driver supplies `board` and `radio_type`. The catalogue is resolved relative to
its own URL; entries with `..`, schemes, or host changes are rejected.

Both catalogue and manifest use `schema_version: 1`. Manifests contain a `latest`
version pointer and a `versions` array. Each release specifies `firmware_version`,
`radio_version`, `release_type` (`beta`, `candidate`, or `release`), a 64-character
`sha256`, and an absolute HTTPS GitHub Release download URL in `file`. No firmware binary is downloaded by
this branch. `latest` must match one of the version entries. Empty manifests use
`latest: null` and `versions: []`.

For ESPHome 2026.10.2, HTTP requests run in a FreeRTOS worker task to keep UART and
TCP handling responsive. TLS certificate verification is required; keep
`http_request.verify_ssl: true` and disallow redirects for metadata.

Security limits: at most 16 KiB per JSON document, 48 versions, and 255 characters
for the persisted override. Schema validation rejects mismatched board/radio and
malformed metadata. A failed request never resets or takes ownership of the radio.
