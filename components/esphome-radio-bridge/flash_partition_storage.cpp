#include "flash_partition_storage.h"

namespace esphome::esphome_radio_bridge::firmware {

std::shared_ptr<FlashPartitionStorage> FlashPartitionStorage::open() {
  const auto* partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "radio_fw");
  if (partition == nullptr) return nullptr;
  return std::shared_ptr<FlashPartitionStorage>(new FlashPartitionStorage(partition));
}

size_t FlashPartitionStorage::capacity() const { return partition_->size; }

bool FlashPartitionStorage::prepare(size_t image_bytes) {
  if (image_bytes == 0 || image_bytes > capacity()) return false;
  image_size_ = 0;
  if (esp_partition_erase_range(partition_, 0, partition_->size) != ESP_OK) return false;
  image_size_ = image_bytes;
  return true;
}

bool FlashPartitionStorage::write(size_t offset, const uint8_t* bytes, size_t length) {
  if (bytes == nullptr || offset > image_size_ || length > image_size_ - offset) return false;
  return esp_partition_write(partition_, offset, bytes, length) == ESP_OK;
}

bool FlashPartitionStorage::read(size_t offset, uint8_t* bytes, size_t length) const {
  if (bytes == nullptr || offset > image_size_ || length > image_size_ - offset) return false;
  return esp_partition_read(partition_, offset, bytes, length) == ESP_OK;
}

}  // namespace esphome::esphome_radio_bridge::firmware
