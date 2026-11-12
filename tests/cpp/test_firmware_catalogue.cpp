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
}
