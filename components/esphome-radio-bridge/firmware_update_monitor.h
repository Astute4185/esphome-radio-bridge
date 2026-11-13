#pragma once

// Radio-agnostic firmware update assessment and scheduling.
// The UART probe reports a radio stack version, not the packaging/release version.
// Do not claim "up to date" when the installed release cannot be identified.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "firmware_catalogue.h"

namespace esphome::esphome_radio_bridge::firmware {

static constexpr uint32_t UPDATE_CHECK_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
static constexpr uint32_t UPDATE_RETRY_INTERVAL_MS = 15UL * 60UL * 1000UL;

enum class UpdateAvailability : uint8_t { UNKNOWN, AVAILABLE, CURRENT };

struct UpdateAssessment {
  UpdateAvailability availability{UpdateAvailability::UNKNOWN};
  const char* reason{"Update not checked"};
};

inline bool update_check_due(bool attempted, uint32_t now_ms, uint32_t last_attempt_ms, bool last_success) {
  if (!attempted) return true;
  const uint32_t interval = last_success ? UPDATE_CHECK_INTERVAL_MS : UPDATE_RETRY_INTERVAL_MS;
  return static_cast<uint32_t>(now_ms - last_attempt_ms) >= interval;
}

// Accept a dotted radio version and the EFR32 probe's optional " (build N)" suffix.
// Ignore trailing zero segments: 9.1.2 and 9.1.2.0 identify the same radio stack.
inline bool parse_radio_version(const std::string& version, std::array<uint32_t, 8>& parts, size_t& count) {
  count = 0;
  if (version.empty()) return false;
  size_t end = version.find(" (build ");
  if (end == std::string::npos) {
    end = version.size();
  } else {
    const auto build_start = end + sizeof(" (build ") - 1;
    if (build_start == version.size() || version.back() != ')') return false;
    for (size_t i = build_start; i + 1 < version.size(); ++i)
      if (version[i] < '0' || version[i] > '9') return false;
  }
  if (end == 0 || version[end - 1] == '.') return false;
  uint32_t segment = 0;
  bool has_digit = false;
  for (size_t i = 0; i < end; ++i) {
    const char ch = version[i];
    if (ch == '.') {
      if (!has_digit || count == parts.size()) return false;
      parts[count++] = segment;
      segment = 0;
      has_digit = false;
    } else if (ch >= '0' && ch <= '9') {
      if (segment > 65535U) return false;
      segment = segment * 10U + static_cast<uint32_t>(ch - '0');
      if (segment > 65535U) return false;
      has_digit = true;
    } else {
      return false;
    }
  }
  if (!has_digit || count == parts.size()) return false;
  parts[count++] = segment;
  while (count > 1 && parts[count - 1] == 0) --count;
  return true;
}

// Returns -1, 0, 1, or 2 for an invalid/unknown comparison.
inline int compare_radio_versions(const std::string& installed, const std::string& candidate) {
  std::array<uint32_t, 8> installed_parts{};
  std::array<uint32_t, 8> candidate_parts{};
  size_t installed_count = 0;
  size_t candidate_count = 0;
  if (!parse_radio_version(installed, installed_parts, installed_count) ||
      !parse_radio_version(candidate, candidate_parts, candidate_count))
    return 2;
  const size_t count = installed_count > candidate_count ? installed_count : candidate_count;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t old_part = i < installed_count ? installed_parts[i] : 0;
    const uint32_t new_part = i < candidate_count ? candidate_parts[i] : 0;
    if (old_part < new_part) return -1;
    if (old_part > new_part) return 1;
  }
  return 0;
}

inline UpdateAssessment assess_update(const FirmwareLoadResult& metadata,
                                      const std::string& installed_radio_version, bool radio_responding) {
  if (!metadata.ok) return {UpdateAvailability::UNKNOWN, "Catalogue not loaded"};
  if (metadata.versions.empty()) return {UpdateAvailability::CURRENT, "No published releases"};
  const FirmwareRelease* latest = highest_firmware_release(metadata);
  if (latest == nullptr) return {UpdateAvailability::UNKNOWN, "Latest release not found"};
  if (!radio_responding) return {UpdateAvailability::UNKNOWN, "Radio version unavailable"};
  const int stack_comparison = compare_radio_versions(installed_radio_version, latest->radio_version);
  if (stack_comparison == 2) return {UpdateAvailability::UNKNOWN, "Radio versions cannot be compared"};
  if (stack_comparison < 0) return {UpdateAvailability::AVAILABLE, "Newer radio stack published"};
  if (stack_comparison > 0) return {UpdateAvailability::CURRENT, "Installed radio stack is newer"};

  // Same radio version: infer the packaging version only when the manifest maps
  // that radio version to exactly one release. Multiple releases of the same
  // radio stack cannot be distinguished by the EFR32 startup probe.
  const FirmwareRelease* matching = nullptr;
  for (const auto& release : metadata.versions) {
    if (compare_radio_versions(installed_radio_version, release.radio_version) != 0) continue;
    if (matching != nullptr)
      return {UpdateAvailability::UNKNOWN, "Multiple releases share this radio version"};
    matching = &release;
  }
  if (matching == nullptr) return {UpdateAvailability::UNKNOWN, "Installed release cannot be identified"};
  if (matching->firmware_version == latest->firmware_version)
    return {UpdateAvailability::CURRENT, "Radio version matches latest release"};
  const int release_comparison =
      compare_firmware_versions(matching->firmware_version, latest->firmware_version);
  return release_comparison < 0
             ? UpdateAssessment{UpdateAvailability::AVAILABLE, "Newer firmware release published"}
             : UpdateAssessment{UpdateAvailability::CURRENT, "Installed release is not older"};
}

}  // namespace esphome::esphome_radio_bridge::firmware
