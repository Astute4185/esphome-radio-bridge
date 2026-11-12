#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::esphome_radio_bridge::firmware {

// Generic, random-access firmware image; can be RAM, flash, or another backend.
class FirmwareImage {
 public:
  virtual ~FirmwareImage() = default;
  virtual size_t size() const = 0;
  virtual bool read(size_t offset, uint8_t* bytes, size_t length) const = 0;
};

class FirmwareStorage : public FirmwareImage {
 public:
  virtual size_t capacity() const = 0;
  virtual bool prepare(size_t image_bytes) = 0;
  virtual bool write(size_t offset, const uint8_t* bytes, size_t length) = 0;
};

}  // namespace esphome::esphome_radio_bridge::firmware
