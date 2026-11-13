#pragma once

// Release selection is based on the unique manifest firmware_version, not the
// radio stack version: multiple hardware variants may share one radio_version.
#include <string>
#include <vector>

#include "firmware_flash_policy.h"

namespace esphome::esphome_radio_bridge::firmware {

struct FirmwareChoice {
  std::string label;
  std::string release_id;  // Empty ID means use highest `firmware_version`.
};

inline std::string firmware_choice_label(const FirmwareRelease& release) {
  // Keep labels readable and safe for HA/API clients even for custom manifests.
  std::string radio = release.radio_version;
  for (char& ch : radio) {
    if (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) == 127) ch = ' ';
  }
  return radio + " (release " + release.firmware_version + ", " + release.release_type + ")";
}

inline std::vector<FirmwareChoice> firmware_choices(const FirmwareLoadResult& metadata) {
  std::vector<FirmwareChoice> choices;
  choices.reserve(metadata.ok ? metadata.versions.size() + 1 : 1);
  choices.push_back({"Latest", ""});
  if (metadata.ok && valid_manifest_metadata(metadata)) {
    for (const auto& release : metadata.versions)
      choices.push_back({firmware_choice_label(release), release.firmware_version});
  }
  return choices;
}

inline bool firmware_choice_exists(const std::vector<FirmwareChoice>& choices, const std::string& id) {
  for (const auto& choice : choices)
    if (choice.release_id == id) return true;
  return false;
}

inline const FirmwareRelease* select_firmware_release(const FirmwareLoadResult& metadata,
                                                      const std::string& release_id) {
  if (release_id.empty()) return select_latest_release(metadata);
  if (!metadata.ok || !valid_manifest_metadata(metadata)) return nullptr;
  for (const auto& release : metadata.versions)
    if (release.firmware_version == release_id) return &release;
  return nullptr;
}

}  // namespace esphome::esphome_radio_bridge::firmware
