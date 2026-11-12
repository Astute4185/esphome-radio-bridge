#include <cassert>
#include <string>

#include "firmware_flash_diagnostics.h"
#include "firmware_flash_policy.h"

using namespace esphome::esphome_radio_bridge::firmware;

int main() {
  FirmwareFlashDiagnostics d;
  assert(d.status() == "Idle");
  assert(d.rejected("arm firmware maintenance first"));
  assert(d.status() == "Blocked: arm firmware maintenance first");
  assert(!d.rejected("arm firmware maintenance first"));
  assert(d.start());
  assert(d.status() == "Downloading 0%");
  assert(!d.download_progress(4));
  assert(d.download_progress(12) && d.status() == "Downloading 10%");
  assert(!d.download_progress(14));
  assert(d.download_progress(100) && d.status() == "Verifying firmware");
  assert(!d.download_progress(99));
  assert(d.verified() && d.status() == "Verified: entering bootloader");
  assert(d.flashing() && d.status() == "Flashing 0%");
  assert(d.flash_progress(63) && d.status() == "Flashing 60%");
  assert(!d.flash_progress(62));
  assert(d.flash_progress(100) && d.status() == "Flashing 100%");
  assert(d.restoring() && d.status() == "Restoring radio");
  assert(d.completed() && d.status() == "Completed");
  assert(!d.rejected("offline"));
  assert(d.status() == "Completed");
  assert(!d.flash_progress(65));
  assert(d.start() && d.status() == "Downloading 0%");
  assert(d.failed("SHA-256\nchecksum mismatch"));
  assert(d.status() == "Failed: SHA-256 checksum mismatch");
  assert(!d.rejected("client connected"));
  assert(d.status() == "Failed: SHA-256 checksum mismatch");
  assert(d.start() && d.status() == "Downloading 0%");
  assert(d.failed(std::string(250, 'X')));
  assert(d.status().size() == std::string("Failed: ").size() + 160);
  assert(d.start());
  assert(d.failed(""));
  assert(d.status() == "Failed: unspecified error");

  FlashPreflight ready{true, false, false, true, true, true, false};
  assert(flash_block_reason(ready) == nullptr);
  ready.client_connected = true;
  assert(std::string(flash_block_reason(ready)) == "disconnect the TCP client first");
  ready.client_connected = false;
  ready.network_ready = false;
  assert(std::string(flash_block_reason(ready)) == "network offline");
  ready.armed = false;
  assert(std::string(flash_block_reason(ready)) == "arm firmware maintenance first");
}
