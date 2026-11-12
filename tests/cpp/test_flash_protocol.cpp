#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "gecko_xmodem.h"

using namespace esphome::esphome_radio_bridge;

class MemoryImage final : public firmware::FirmwareImage {
 public:
  size_t size() const override { return 3; }
  bool read(size_t offset, uint8_t* bytes, size_t length) const override {
    if (offset > size() || length > size() - offset) return false;
    const uint8_t sample[] = {1, 2, 3};
    for (size_t i = 0; i < length; ++i) bytes[i] = sample[offset + i];
    return true;
  }
};

class MockUart : public identification::ProbeUart {
 public:
  int read(uint8_t* out, size_t capacity) override {
    size_t n = 0;
    while (n < capacity && !rx.empty()) {
      out[n++] = rx.front();
      rx.pop_front();
    }
    return static_cast<int>(n);
  }
  int write(const uint8_t* bytes, size_t length) override {
    tx.insert(tx.end(), bytes, bytes + length);
    return static_cast<int>(length);
  }
  void flush_input() override { rx.clear(); }
  void inject(const std::string& message) {
    for (char c : message) rx.push_back(static_cast<uint8_t>(c));
  }
  void inject(uint8_t byte) { rx.push_back(byte); }
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
};

int main() {
  firmware::GeckoXmodem flash;
  MemoryImage image;
  MockUart uart;
  assert(flash.begin(&image, 0));
  assert(!flash.begin(&image, 0));
  uart.inject("Gecko Bootloader\r\nBL >");
  flash.poll(uart, 10);
  flash.poll(uart, 20);
  assert(!uart.tx.empty() && uart.tx.back() == '1');
  uart.inject('C');
  flash.poll(uart, 30);
  flash.poll(uart, 40);
  assert(uart.tx.size() == 134);
  assert(uart.tx[1] == 0x01);  // SOH
  assert(uart.tx[2] == 1);     // Block 1
  assert(uart.tx[3] == 0xFE);  // Complement
  assert(uart.tx[4] == 1 && uart.tx[5] == 2 && uart.tx[6] == 3);
  assert(uart.tx[7] == 0x1a);  // Padding
  uart.inject(0x06);           // ACK block
  flash.poll(uart, 50);
  assert(flash.percent() == 100);
  flash.poll(uart, 60);
  assert(uart.tx.back() == 0x04);  // EOT
  uart.inject(0x06);
  flash.poll(uart, 70);
  uart.inject("Serial upload complete\r\nBL >");
  flash.poll(uart, 80);
  assert(flash.succeeded());
  assert(!flash.running());
  firmware::GeckoXmodem cancelled;
  assert(cancelled.begin(&image, 0));
  uart.inject(0x18);  // CAN
  cancelled.poll(uart, 10);
  assert(cancelled.failed());
  firmware::GeckoXmodem timed_out;
  assert(timed_out.begin(&image, 0));
  timed_out.poll(uart, 16000);
  assert(timed_out.failed());
}
