#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome::esphome_radio_bridge::identification {

// Radio protocol implementations own their wire exchanges. The generic bridge
// grants exclusive UART access for startup and explicitly requested probes.
class ProbeUart {
 public:
  virtual ~ProbeUart() = default;
  virtual int read(uint8_t* data, size_t length) = 0;
  virtual int write(const uint8_t* data, size_t length) = 0;
  virtual void flush_input() = 0;
};

enum class ProbeState : uint8_t { UNSUPPORTED, RUNNING, COMPLETE, FAILED };

struct RadioIdentity {
  std::string firmware{"Unknown"};
  std::string status{"Not probed"};
  bool responding{false};
  uint8_t protocol_version{0};
};

class RadioProbe {
 public:
  virtual ~RadioProbe() = default;
  virtual ProbeState begin(ProbeUart& uart, uint32_t now_ms) = 0;
  virtual ProbeState poll(ProbeUart& uart, uint32_t now_ms) = 0;
  virtual void cancel() = 0;
  virtual const RadioIdentity& identity() const = 0;
};

}  // namespace esphome::esphome_radio_bridge::identification
