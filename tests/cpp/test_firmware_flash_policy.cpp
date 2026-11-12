#include <cassert>
#include <string>

#include "firmware_flash_policy.h"

using namespace esphome::esphome_radio_bridge::firmware;

int main() {
  FlashPreflight ready{true, false, false, true, true, true, false};
  assert(can_flash(ready));
  ready.client_connected = true;
  assert(!can_flash(ready));
  ready.client_connected = false;
  ready.radio_identifying = true;
  assert(!can_flash(ready));
  ready.radio_identifying = false;
  ready.armed = false;
  assert(!can_flash(ready));
  ready.armed = true;
  ready.already_flashing = true;
  assert(!can_flash(ready));
  ready.already_flashing = false;
  ready.network_ready = false;
  assert(!can_flash(ready));

  FirmwareLoadResult metadata;
  metadata.ok = true;
  metadata.latest = "1.0";
  assert(select_latest_release(metadata) == nullptr);
  metadata.versions.push_back({"1.0", "9.1.2", "release", std::string(64, 'a'),
                               "https://github.com/example/repo/releases/download/v1/fw.gbl"});
  const FirmwareRelease* selected = select_latest_release(metadata);
  assert(selected != nullptr && selected->firmware_version == "1.0");
  metadata.board = "different_board";
  metadata.versions[0].sha256 = "invalid";
  assert(select_latest_release(metadata) == nullptr);
}
