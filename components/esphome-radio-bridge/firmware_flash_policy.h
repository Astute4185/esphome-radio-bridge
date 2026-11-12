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

inline bool can_flash(const FlashPreflight& state) {
  return state.armed && !state.client_connected && !state.radio_identifying && state.normal_mode &&
         state.network_ready && state.configured && !state.already_flashing;
}

inline const FirmwareRelease* select_latest_release(const FirmwareLoadResult& metadata) {
  if (!metadata.ok || !valid_manifest_metadata(metadata)) return nullptr;
  for (const auto& release : metadata.versions)
    if (release.firmware_version == metadata.latest) return &release;
  return nullptr;
}

}  // namespace esphome::esphome_radio_bridge::firmware
