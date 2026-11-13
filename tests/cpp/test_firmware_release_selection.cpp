#include <cassert>
#include <string>
#include <vector>

#include "firmware_release_selection.h"

using namespace esphome::esphome_radio_bridge::firmware;

int main() {
  FirmwareLoadResult manifest;
  auto missing = firmware_choices(manifest);
  assert(missing.size() == 1);
  assert(missing[0].label == "Latest");
  assert(select_firmware_release(manifest, "") == nullptr);

  manifest.ok = true;
  manifest.board = "tubeszb_mgm210_poe_2023";
  manifest.radio_type = "zha";
  manifest.latest = "0.2";
  manifest.versions = {
      {"0.1", "9.1.2", "beta", std::string(64, 'a'),
       "https://github.com/owner/repo/releases/download/v1/fw-2wire.gbl"},
      {"0.2", "9.1.2", "release", std::string(64, 'b'),
       "https://github.com/owner/repo/releases/download/v2/fw-4wire.gbl"},
  };
  auto choices = firmware_choices(manifest);
  assert(choices.size() == 3);
  assert(choices[0].label == "Latest");
  assert(choices[1].label == "9.1.2 (release 0.1, beta)");
  assert(choices[2].label == "9.1.2 (release 0.2, release)");
  assert(choices[1].label != choices[2].label);
  assert(choices[1].release_id == "0.1");
  assert(choices[2].release_id == "0.2");
  assert(firmware_choice_exists(choices, ""));
  assert(firmware_choice_exists(choices, "0.1"));
  assert(!firmware_choice_exists(choices, "0.3"));
  assert(select_firmware_release(manifest, "")->firmware_version == "0.2");
  assert(select_firmware_release(manifest, "0.1")->file.find("2wire") != std::string::npos);
  assert(select_firmware_release(manifest, "0.2")->file.find("4wire") != std::string::npos);
  assert(select_firmware_release(manifest, "0.3") == nullptr);

  // Regression: an old `latest` pointer and unsorted releases must not default
  // to 7.4.3 when the catalogue contains the 9.1.2 variants.
  manifest.latest = "0.1";
  manifest.versions = {
      {"1.0", "9.1.2.2P", "release", std::string(64, 'c'),
       "https://github.com/owner/repo/releases/download/v3/fw-2wire.gbl"},
      {"0.1", "7.4.3", "release", std::string(64, 'a'),
       "https://github.com/owner/repo/releases/download/v1/fw-old.gbl"},
      {"1.1", "9.1.2.4P", "release", std::string(64, 'b'),
       "https://github.com/owner/repo/releases/download/v3/fw-4wire.gbl"},
  };
  assert(valid_manifest_metadata(manifest));
  assert(select_firmware_release(manifest, "")->firmware_version == "1.1");
  assert(select_firmware_release(manifest, "")->radio_version == "9.1.2.4P");
  assert(select_firmware_release(manifest, "1.0")->radio_version == "9.1.2.2P");
  assert(select_firmware_release(manifest, "0.1")->radio_version == "7.4.3");
  assert(latest_radio_version(manifest) == "9.1.2.4P");

  // A new custom manifest defines a separate set of valid choices, not a union
  // with the previous source. Old selections are invalid on source replacement.
  FirmwareLoadResult custom = manifest;
  custom.latest = "0.7";
  custom.versions = {{"0.7", "9.3.1", "candidate", std::string(64, 'c'),
                      "https://github.com/owner/other/releases/download/v7/fw.gbl"}};
  const auto custom_choices = firmware_choices(custom);
  assert(custom_choices.size() == 2);
  assert(!firmware_choice_exists(custom_choices, "0.1"));
  assert(select_firmware_release(custom, "0.1") == nullptr);
  assert(select_firmware_release(custom, "")->radio_version == "9.3.1");

  // Invalid and failed manifests may never expose or flash stale releases.
  custom.ok = false;
  assert(firmware_choices(custom).size() == 1);
  assert(select_firmware_release(custom, "0.7") == nullptr);
  custom.ok = true;
  custom.versions.front().sha256 = "bad";
  assert(firmware_choices(custom).size() == 1);
  assert(select_firmware_release(custom, "") == nullptr);

  FirmwareRelease ugly = manifest.versions.front();
  ugly.radio_version = "9.1.2\nINJECTED";
  assert(firmware_choice_label(ugly).find('\n') == std::string::npos);
}
