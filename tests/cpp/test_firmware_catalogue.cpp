#include <cassert>
#include <string>

#include "firmware_catalogue.h"

using namespace esphome::esphome_radio_bridge::firmware;

int main() {
  assert(is_https_url("https://github.com/owner/repo/releases/download/v0.1/firmware.gbl"));
  assert(!is_https_url("http://example.org/catalogue.json"));
  assert(!is_https_url("https://"));
  assert(!is_https_url("https://example.org/space here"));
  assert(!is_https_url("https://example.org\\foo"));
  assert(!is_https_url("https://example.org@bad.example/path"));
  assert(!is_https_url("https://example.org/path#fragment"));
  assert(is_github_release_url("https://github.com/owner/repo/releases/download/v0.1/fw.gbl"));
  assert(!is_github_release_url("https://example.org/firmware.gbl"));
  assert(!is_github_release_url("https://github.com/owner/repo/archive/file.gbl"));

  std::string result;
  const std::string catalogue = "https://raw.githubusercontent.com/owner/repo/main/catalogue.json";
  assert(resolve_manifest_url(catalogue, "firmware/tubeszb/zha/manifest.json", result));
  assert(result == "https://raw.githubusercontent.com/owner/repo/main/firmware/tubeszb/zha/manifest.json");
  assert(!resolve_manifest_url(catalogue, "../firmware/manifest.json", result));
  assert(!resolve_manifest_url(catalogue, "https://bad.example/manifest.json", result));
  assert(!resolve_manifest_url(catalogue, "/other/manifest.json", result));
  assert(!resolve_manifest_url(catalogue, "firmware\\manifest.json", result));
  assert(!resolve_manifest_url(catalogue, "firmware/manifest.json?token=1", result));

  // Driver-selected default is used only when YAML does not specify an override.
  assert(select_catalogue_url("", nullptr).empty());
  assert(select_catalogue_url("", "https://example.com/driver/catalogue.json") ==
         "https://example.com/driver/catalogue.json");
  assert(
      select_catalogue_url("https://example.com/custom.json", "https://example.com/driver/catalogue.json") ==
      "https://example.com/custom.json");
  assert(select_catalogue_url("https://example.com/custom.json", nullptr) ==
         "https://example.com/custom.json");

  // Real driver default + repository-root-relative catalogue path.
  const std::string driver_default =
      "https://raw.githubusercontent.com/Astute4185/esphome-radio-bridge/main/firmware/catalogue.json";
  assert(resolve_manifest_url(driver_default, "firmware/tubeszb_mgm210_poe_2023/zha/manifest.json", result));
  assert(result ==
         "https://raw.githubusercontent.com/Astute4185/esphome-radio-bridge/main/firmware/"
         "tubeszb_mgm210_poe_2023/zha/manifest.json");

  // The catalogue may be hosted inside firmware/, while the entry is rooted at
  // the repository's firmware/ directory. Avoid duplicating that segment.
  const std::string nested = "https://raw.githubusercontent.com/owner/repo/main/firmware/catalogue.json";
  assert(resolve_manifest_url(nested, "firmware/tubeszb/zha/manifest.json", result));
  assert(result == "https://raw.githubusercontent.com/owner/repo/main/firmware/tubeszb/zha/manifest.json");

  // Ordinary directory-relative entries must remain supported.
  assert(resolve_manifest_url(nested, "tubeszb/zha/manifest.json", result));
  assert(result == "https://raw.githubusercontent.com/owner/repo/main/firmware/tubeszb/zha/manifest.json");

  // Explicit ./ overrides overlap handling when a repeated directory is intentional.
  assert(resolve_manifest_url(nested, "./firmware/manifest.json", result));
  assert(result == "https://raw.githubusercontent.com/owner/repo/main/firmware/firmware/manifest.json");

  // No repository name, branch, tag or hosting-directory structure is hardcoded.
  const std::string tagged = "https://cdn.example.org/releases/v3.2/firmware/catalogue.json";
  assert(resolve_manifest_url(tagged, "firmware/board/radio/manifest.json", result));
  assert(result == "https://cdn.example.org/releases/v3.2/firmware/board/radio/manifest.json");

  // Reject attempts to escape the catalogue origin or hide traversal with encoding.
  assert(!resolve_manifest_url(nested, "./../manifest.json", result));
  assert(!resolve_manifest_url(nested, "firmware/%2e%2e/manifest.json", result));
  assert(!resolve_manifest_url(nested, "https://evil.example/manifest.json", result));

  assert(valid_firmware_version("0.1"));
  assert(valid_firmware_version("3.2.10"));
  assert(!valid_firmware_version(""));
  assert(!valid_firmware_version("1..2"));
  assert(!valid_firmware_version("1.2-beta"));

  assert(valid_sha256(std::string(64, 'a')));
  assert(valid_sha256(std::string(64, 'F')));
  assert(!valid_sha256(std::string(63, 'a')));
  assert(!valid_sha256(std::string(64, 'g')));

  FirmwareRelease release{"0.2", "9.1.2", "release", std::string(64, 'a'),
                          "https://github.com/owner/repo/releases/download/v0.2/fw.gbl"};
  assert(valid_release(release));
  FirmwareLoadResult manifest;
  manifest.latest = "0.2";
  manifest.versions.push_back(release);
  assert(valid_manifest_metadata(manifest));
  manifest.latest = "0.3";
  assert(!valid_manifest_metadata(manifest));
  manifest.latest = "0.2";
  manifest.versions.push_back(release);
  assert(!valid_manifest_metadata(manifest));  // Duplicate firmware version
  manifest.versions.pop_back();
  manifest.versions.front().sha256 = "invalid";
  assert(!valid_manifest_metadata(manifest));
  manifest.versions.front() = release;
  manifest.versions.front().release_type = "superseded";
  assert(!valid_manifest_metadata(manifest));
  manifest.versions.clear();
  manifest.latest.clear();
  assert(valid_manifest_metadata(manifest));  // Valid empty catalogue

  // Published TubesZB beta manifest must satisfy the same metadata contract
  // as the on-device parser. A valid release is independent of download reachability.
  FirmwareLoadResult published;
  published.board = "tubeszb_mgm210_poe_2023";
  published.radio_type = "zha";
  published.latest = "0.1";
  published.versions.push_back({"0.1", "9.1.2", "beta",
                                "7bdbf32af5b11f3ce08ee60828bf65d8ba3490794b903076dd0d6a125234e76d",
                                "https://github.com/Astute4185/tubeszb_mgm210_firmware/releases/download/"
                                "DevSample-4v1/tubeszb_mgm210_zha_2023_9_1_2_9.1.2.0.gbl"});
  assert(valid_manifest_metadata(published));
  assert(manifest_validation_error(published) == nullptr);
  assert(latest_radio_version(published) == "9.1.2");
  // Display the actual radio version, not the release/package ID (0.1).
  published.versions.push_back({"0.2", "9.2.1", "release", std::string(64, 'b'),
                                "https://github.com/owner/repo/releases/download/v0.2/fw.gbl"});
  published.latest = "0.2";
  assert(latest_radio_version(published) == "9.2.1");
  published.latest = "0.1";
  assert(latest_radio_version(published) == "9.1.2");
  published.latest = "0.3";
  assert(latest_radio_version(published) == "Unknown");
  published.latest.clear();
  assert(latest_radio_version(published) == "None");
  published.latest = "0.1";
  published.versions.pop_back();
  published.versions.front().sha256 = "invalid";
  assert(std::string(manifest_validation_error(published)) ==
         "Invalid sha256 (expected 64 hexadecimal characters)");
  published.versions.front().sha256 = std::string(64, 'a');
  published.versions.front().file = "http://example.org/firmware.gbl";
  assert(std::string(manifest_validation_error(published)) ==
         "Invalid file (expected HTTPS GitHub Release URL)");
  published.versions.front().file = release.file;
  published.versions.front().release_type = "superseded";
  assert(std::string(manifest_validation_error(published)) ==
         "Invalid release_type (expected beta, candidate or release)");
}
