#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "firmware_storage.h"
#include "radio_identification.h"

namespace esphome::esphome_radio_bridge::firmware {

// Radio-specific transfer protocol. A board driver selects its implementation.
// The bridge owns UART arbitration; protocols never open TCP sockets or manage GPIO.
class FlashProtocol {
 public:
  virtual ~FlashProtocol() = default;
  virtual bool begin(const FirmwareImage* image, uint32_t now_ms) = 0;
  virtual void poll(identification::ProbeUart& uart, uint32_t now_ms) = 0;
  virtual unsigned percent() const = 0;
  virtual bool running() const = 0;
  virtual bool succeeded() const = 0;
  virtual bool failed() const = 0;
  virtual const std::string& error() const = 0;
  virtual void cancel() = 0;
};

}  // namespace esphome::esphome_radio_bridge::firmware
