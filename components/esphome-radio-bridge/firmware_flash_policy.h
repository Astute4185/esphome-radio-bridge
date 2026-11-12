#pragma once

#include "firmware_catalogue.h"

namespace esphome::esphome_radio_bridge::firmware {

// Pure preflight conditions: no flash without explicit arming and exclusive UART.
struct FlashPreflight {
  bool armed{false};
  bool client_connected{false};
  bool radio_identifying{false};
  bool normal_mode{false};
  bool network_ready{false};
  bool configured{false};
  bool already_flashing{false};
};

inline const char* flash_block_reason(const FlashPreflight& state) {
  if (state.already_flashing) return "another update is in progress";
  if (!state.configured) return "flashing is not configured or ready";
  if (!state.armed) return "arm firmware maintenance first";
  if (state.client_connected) return "disconnect the TCP client first";
  if (state.radio_identifying) return "radio identification is active";
  if (!state.normal_mode) return "radio is not in normal mode";
  if (!state.network_ready) return "network offline";
  return nullptr;
}

inline bool can_flash(const FlashPreflight& state) { return flash_block_reason(state) == nullptr; }

inline const FirmwareRelease* select_latest_release(const FirmwareLoadResult& metadata) {
  if (!metadata.ok || !valid_manifest_metadata(metadata)) return nullptr;
  for (const auto& release : metadata.versions)
    if (release.firmware_version == metadata.latest) return &release;
  return nullptr;
}

}  // namespace esphome::esphome_radio_bridge::firmware
