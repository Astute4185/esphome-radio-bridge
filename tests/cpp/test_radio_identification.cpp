#include <cassert>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "efr32_ezsp_probe.h"

using namespace esphome::esphome_radio_bridge::identification;

struct MockUart : ProbeUart {
  std::deque<uint8_t> incoming;
  std::vector<std::vector<uint8_t>> sent;
  int read(uint8_t* out, size_t size) override {
    const auto count = size < incoming.size() ? size : incoming.size();
    for (size_t i = 0; i < count; ++i) {
      out[i] = incoming.front();
      incoming.pop_front();
    }
    return static_cast<int>(count);
  }
  int write(const uint8_t* data, size_t length) override {
    sent.emplace_back(data, data + length);
    return static_cast<int>(length);
  }
  void flush_input() override { incoming.clear(); }
  void receive(std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) incoming.push_back(b);
  }
};

// Independently build known ASH frames: CRC16-CCITT and escaping.
static uint16_t crc16(const std::vector<uint8_t>& data) {
  uint16_t crc = 0xFFFF;
  for (uint8_t b : data) {
    crc ^= uint16_t(b) << 8;
    for (int bit = 0; bit < 8; ++bit) crc = crc & 0x8000 ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}
static void send_frame(MockUart& uart, std::vector<uint8_t> frame) {
  const uint16_t crc = crc16(frame);
  frame.push_back(static_cast<uint8_t>(crc >> 8));
  frame.push_back(static_cast<uint8_t>(crc));
  for (uint8_t b : frame) {
    if (b == 0x7E || b == 0x7D || b == 0x11 || b == 0x13 || b == 0x18 || b == 0x1A) {
      uart.incoming.push_back(0x7D);
      uart.incoming.push_back(b ^ 0x20);
    } else {
      uart.incoming.push_back(b);
    }
  }
  uart.incoming.push_back(0x7E);
}
static void send_ezsp(MockUart& uart, uint8_t ash_ctrl, std::vector<uint8_t> ezsp) {
  uint8_t rnd = 0x42;
  for (auto& b : ezsp) {
    b ^= rnd;
    rnd = (rnd & 1) ? static_cast<uint8_t>((rnd >> 1) ^ 0xB8) : static_cast<uint8_t>(rnd >> 1);
  }
  std::vector<uint8_t> data{ash_ctrl};
  data.insert(data.end(), ezsp.begin(), ezsp.end());
  send_frame(uart, data);
}
int main() {
  MockUart uart;
  Efr32EzspProbe probe;
  assert(probe.begin(uart, 100) == ProbeState::RUNNING);
  assert(uart.sent.empty());
  assert(probe.poll(uart, 1599) == ProbeState::RUNNING);
  assert(uart.sent.empty());
  assert(probe.poll(uart, 1600) == ProbeState::RUNNING);
  assert(uart.sent.size() == 1);
  assert(uart.sent.front() == std::vector<uint8_t>({0x1A, 0xC0, 0x38, 0xBC, 0x7E}));

  // A CRC-corrupted RSTACK must not count as a responsive coordinator.
  uart.receive({0xC1, 0x02, 0x0B, 0x00, 0x00, 0x7E});
  assert(probe.poll(uart, 1700) == ProbeState::RUNNING);
  assert(!probe.identity().responding);

  // RSTACK, legacy VERSION and extended VERSION, all on a single UART owner.
  send_frame(uart, {0xC1, 0x02, 0x0B});
  assert(probe.poll(uart, 1800) == ProbeState::RUNNING);
  assert(probe.identity().responding);
  assert(uart.sent.size() == 3);  // Reset, ASH ACK(0), legacy version DATA.

  send_ezsp(uart, 0x00, {0x00, 0x80, 0x00, 0x0E, 0x02, 0x10, 0x09});
  assert(probe.poll(uart, 1900) == ProbeState::RUNNING);
  assert(uart.sent.size() == 5);  // ASH ACK(1), extended version DATA.
  send_ezsp(uart, 0x10, {0x01, 0x80, 0x01, 0x00, 0x00, 0x0E, 0x02, 0x10, 0x91});
  assert(probe.poll(uart, 2000) == ProbeState::RUNNING);
  assert(probe.identity().firmware == "9.1.1.0");
  send_ezsp(uart, 0x20,
            {0x02, 0x80, 0x01, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x34, 0x12, 0x09, 0x01, 0x02, 0x00});
  assert(probe.poll(uart, 2100) == ProbeState::COMPLETE);
  assert(probe.identity().firmware == "9.1.2.0 (build 4660)");
  assert(probe.identity().status == "Identified");

  MockUart disconnected;
  Efr32EzspProbe timeout;
  assert(timeout.begin(disconnected, 0xFFFFFFF0) == ProbeState::RUNNING);
  assert(timeout.poll(disconnected, 0x000005CC) == ProbeState::RUNNING);  // 1500 after wrap
  assert(timeout.poll(disconnected, 0x00001183) == ProbeState::RUNNING);  // 2999 after RST
  assert(timeout.poll(disconnected, 0x00001184) == ProbeState::FAILED);
  assert(!timeout.identity().responding);
  assert(timeout.identity().firmware == "Unknown");
  timeout.cancel();
  assert(timeout.poll(disconnected, 0) == ProbeState::UNSUPPORTED);

  MockUart partial;
  Efr32EzspProbe partial_probe;
  partial_probe.begin(partial, 0);
  partial_probe.poll(partial, 1500);  // start ASH reset
  send_frame(partial, {0xC1, 0x02, 0x0B});
  partial_probe.poll(partial, 1600);
  assert(partial_probe.poll(partial, 3600) == ProbeState::COMPLETE);
  assert(partial_probe.identity().responding);
  assert(partial_probe.identity().firmware == "Unknown");
}
