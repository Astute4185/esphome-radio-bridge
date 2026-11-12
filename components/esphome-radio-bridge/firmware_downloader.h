#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "firmware_catalogue.h"
#include "firmware_storage.h"

namespace esphome::esphome_radio_bridge::firmware {

// Stage is held on a dedicated ESP32 data partition, not in PSRAM/heap.
struct DownloadedFirmware {
  bool ok{false};
  std::string message;
  std::shared_ptr<const FirmwareImage> image;
};

// Only the worker owns the staging partition while downloading. The main ESPHome
// loop claims UART ownership only after SHA-256 has been fully verified.
class FirmwareDownloader {
 public:
  bool start(http_request::HttpRequestComponent* request, const FirmwareRelease& release);
  bool collect(DownloadedFirmware& out);
  bool busy() const { return job_ != nullptr; }
  unsigned progress_percent() const;

 private:
  struct Job;
  static void worker_(void* param);
  Job* job_{nullptr};
};

}  // namespace esphome::esphome_radio_bridge::firmware
