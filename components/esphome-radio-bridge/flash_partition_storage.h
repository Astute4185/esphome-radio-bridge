#pragma once

#include <memory>

#include "esp_partition.h"
#include "firmware_storage.h"

namespace esphome::esphome_radio_bridge::firmware {

// A dedicated data partition, never an app/OTA/NVS partition. Its name is
// deliberately fixed so firmware images cannot overwrite user data.
class FlashPartitionStorage final : public FirmwareStorage {
 public:
  static std::shared_ptr<FlashPartitionStorage> open();
  size_t capacity() const override;
  size_t size() const override { return image_size_; }
  bool prepare(size_t image_bytes) override;
  bool write(size_t offset, const uint8_t* bytes, size_t length) override;
  bool read(size_t offset, uint8_t* bytes, size_t length) const override;

 private:
  explicit FlashPartitionStorage(const esp_partition_t* partition) : partition_(partition) {}
  const esp_partition_t* partition_{nullptr};
  size_t image_size_{0};
};

}  // namespace esphome::esphome_radio_bridge::firmware
