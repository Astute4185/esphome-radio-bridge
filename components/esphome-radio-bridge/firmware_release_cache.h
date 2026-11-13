#pragma once

// Small, bounded NVS snapshot for the firmware dropdown. This cache is for
// display/selection only: an actual flash ALWAYS requires a freshly validated
// manifest with the full download URL and SHA-256 metadata.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "firmware_release_selection.h"

namespace esphome::esphome_radio_bridge::firmware {

constexpr uint32_t RELEASE_CACHE_MAGIC = 0x52424346;  // "FCBR", wire-independent
constexpr uint16_t RELEASE_CACHE_VERSION = 1;
constexpr size_t RELEASE_CACHE_MAX_CHOICES = 24;
constexpr size_t RELEASE_CACHE_LABEL_BYTES = 96;
constexpr size_t RELEASE_CACHE_RELEASE_ID_BYTES = 33;

struct CachedFirmwareChoice {
  char label[RELEASE_CACHE_LABEL_BYTES];
  char release_id[RELEASE_CACHE_RELEASE_ID_BYTES];
};

struct FirmwareChoicesSnapshot {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint64_t source_key;
  char selected_release_id[RELEASE_CACHE_RELEASE_ID_BYTES];
  CachedFirmwareChoice choices[RELEASE_CACHE_MAX_CHOICES];
};

inline uint64_t firmware_source_key(const std::string& board, const std::string& radio_type,
                                    const std::string& catalogue_url,
                                    const std::string& manifest_override_url) {
  uint64_t hash = 14695981039346656037ULL;
  for (const auto* part : {&board, &radio_type, &catalogue_url, &manifest_override_url}) {
    for (unsigned char c : *part) {
      hash ^= c;
      hash *= 1099511628211ULL;
    }
    hash ^= 0xff;  // Separate parts so concatenation cannot alias.
    hash *= 1099511628211ULL;
  }
  return hash;
}

inline bool cache_string(const std::string& value, char* destination, size_t capacity) {
  if (value.size() >= capacity) return false;
  std::memcpy(destination, value.c_str(), value.size() + 1);
  return true;
}

inline bool make_firmware_choices_snapshot(const std::vector<FirmwareChoice>& choices, uint64_t source_key,
                                           const std::string& selected_id, FirmwareChoicesSnapshot& out) {
  if (source_key == 0 || choices.empty() || choices.size() > RELEASE_CACHE_MAX_CHOICES ||
      choices[0].label != "Latest" || !choices[0].release_id.empty() ||
      !firmware_choice_exists(choices, selected_id))
    return false;
  // Fill the caller's buffer directly; allocating two ~3 KiB snapshots on
  // nested ESP32 loop-task stacks risks a stack overflow.
  std::memset(&out, 0, sizeof(out));
  out.magic = RELEASE_CACHE_MAGIC;
  out.version = RELEASE_CACHE_VERSION;
  out.source_key = source_key;
  out.count = static_cast<uint16_t>(choices.size());
  if (!cache_string(selected_id, out.selected_release_id, sizeof(out.selected_release_id))) return false;
  for (size_t i = 0; i < choices.size(); ++i) {
    const auto& choice = choices[i];
    if (choice.label.empty() || (i > 0 && !valid_firmware_version(choice.release_id)) ||
        !cache_string(choice.label, out.choices[i].label, sizeof(out.choices[i].label)) ||
        !cache_string(choice.release_id, out.choices[i].release_id, sizeof(out.choices[i].release_id)))
      return false;
  }
  return true;
}

inline bool read_firmware_choices_snapshot(const FirmwareChoicesSnapshot& snapshot, uint64_t source_key,
                                           std::vector<FirmwareChoice>& out, std::string& selected_id) {
  if (!source_key || snapshot.magic != RELEASE_CACHE_MAGIC || snapshot.version != RELEASE_CACHE_VERSION ||
      snapshot.source_key != source_key || snapshot.count < 1 || snapshot.count > RELEASE_CACHE_MAX_CHOICES)
    return false;
  auto load_string = [](const char* data, size_t capacity, std::string& value) {
    const auto* terminator = static_cast<const char*>(std::memchr(data, '\0', capacity));
    if (terminator == nullptr) return false;
    value.assign(data, static_cast<size_t>(terminator - data));
    for (unsigned char c : value)
      if (c < 32 || c == 127) return false;
    return true;
  };
  std::vector<FirmwareChoice> choices;
  choices.reserve(snapshot.count);
  for (size_t i = 0; i < snapshot.count; ++i) {
    FirmwareChoice choice;
    if (!load_string(snapshot.choices[i].label, sizeof(snapshot.choices[i].label), choice.label) ||
        !load_string(snapshot.choices[i].release_id, sizeof(snapshot.choices[i].release_id),
                     choice.release_id) ||
        choice.label.empty())
      return false;
    if (i == 0 && (choice.label != "Latest" || !choice.release_id.empty())) return false;
    if (i > 0 && !valid_firmware_version(choice.release_id)) return false;
    for (const auto& prior : choices)
      if (choice.label == prior.label || choice.release_id == prior.release_id) return false;
    choices.push_back(std::move(choice));
  }
  std::string stored_id;
  if (!load_string(snapshot.selected_release_id, sizeof(snapshot.selected_release_id), stored_id) ||
      !firmware_choice_exists(choices, stored_id))
    return false;
  out = std::move(choices);
  selected_id = std::move(stored_id);
  return true;
}

inline bool same_firmware_choices(const std::vector<FirmwareChoice>& a,
                                  const std::vector<FirmwareChoice>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].release_id != b[i].release_id || a[i].label != b[i].label) return false;
  return true;
}

}  // namespace esphome::esphome_radio_bridge::firmware
