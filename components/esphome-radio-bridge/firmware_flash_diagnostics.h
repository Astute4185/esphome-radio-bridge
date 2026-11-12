#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome::esphome_radio_bridge::firmware {

// Chip-independent presentation model for a single Home Assistant text sensor.
// Terminal results remain visible until another accepted update attempt begins.
enum class FlashDiagnosticStage : uint8_t {
  IDLE,
  BLOCKED,
  DOWNLOADING,
  VERIFYING,
  VERIFIED,
  FLASHING,
  RESTORING,
  COMPLETED,
  FAILED,
};

class FirmwareFlashDiagnostics {
 public:
  const std::string& status() const { return status_; }
  FlashDiagnosticStage stage() const { return stage_; }

  bool start() {
    stage_ = FlashDiagnosticStage::DOWNLOADING;
    last_percent_ = 0;
    return this->set_("Downloading 0%");
  }

  bool download_progress(unsigned percent) {
    if (stage_ != FlashDiagnosticStage::DOWNLOADING) return false;
    if (percent >= 100) {
      stage_ = FlashDiagnosticStage::VERIFYING;
      return this->set_("Verifying firmware");
    }
    return this->progress_("Downloading ", percent);
  }

  bool verified() {
    if (stage_ != FlashDiagnosticStage::DOWNLOADING && stage_ != FlashDiagnosticStage::VERIFYING)
      return false;
    stage_ = FlashDiagnosticStage::VERIFIED;
    return this->set_("Verified: entering bootloader");
  }

  bool flashing() {
    if (stage_ != FlashDiagnosticStage::VERIFIED) return false;
    stage_ = FlashDiagnosticStage::FLASHING;
    last_percent_ = 0;
    return this->set_("Flashing 0%");
  }

  bool flash_progress(unsigned percent) {
    if (stage_ != FlashDiagnosticStage::FLASHING) return false;
    return this->progress_("Flashing ", percent);
  }

  bool restoring() {
    if (stage_ != FlashDiagnosticStage::FLASHING && stage_ != FlashDiagnosticStage::VERIFIED) return false;
    stage_ = FlashDiagnosticStage::RESTORING;
    return this->set_("Restoring radio");
  }

  bool completed() {
    stage_ = FlashDiagnosticStage::COMPLETED;
    return this->set_("Completed");
  }

  bool failed(const std::string& reason) {
    stage_ = FlashDiagnosticStage::FAILED;
    return this->set_("Failed: " + this->safe_reason_(reason));
  }

  bool rejected(const char* reason) {
    // A rejected button press must not erase a previous terminal result.
    if (stage_ == FlashDiagnosticStage::COMPLETED || stage_ == FlashDiagnosticStage::FAILED ||
        stage_ == FlashDiagnosticStage::DOWNLOADING || stage_ == FlashDiagnosticStage::VERIFYING ||
        stage_ == FlashDiagnosticStage::VERIFIED || stage_ == FlashDiagnosticStage::FLASHING ||
        stage_ == FlashDiagnosticStage::RESTORING)
      return false;
    stage_ = FlashDiagnosticStage::BLOCKED;
    return this->set_("Blocked: " + this->safe_reason_(reason == nullptr ? "unknown" : reason));
  }

 private:
  bool set_(const std::string& value) {
    if (status_ == value) return false;
    status_ = value;
    return true;
  }

  bool progress_(const char* label, unsigned percent) {
    const unsigned bounded = std::min(percent, 100U);
    const unsigned bucket = bounded == 100 ? 100 : (bounded / 5U) * 5U;
    if (bucket <= last_percent_) return false;
    last_percent_ = bucket;
    return this->set_(std::string(label) + std::to_string(bucket) + "%");
  }

  static std::string safe_reason_(const std::string& reason) {
    std::string safe;
    safe.reserve(std::min(reason.size(), static_cast<size_t>(160)));
    for (char ch : reason) {
      if (safe.size() >= 160) break;
      const unsigned char byte = static_cast<unsigned char>(ch);
      safe.push_back(byte >= 32 && byte != 127 ? ch : ' ');
    }
    return safe.empty() ? "unspecified error" : safe;
  }

  FlashDiagnosticStage stage_{FlashDiagnosticStage::IDLE};
  std::string status_{"Idle"};
  unsigned last_percent_{0};
};

}  // namespace esphome::esphome_radio_bridge::firmware
