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
  // Without unsolicited menu bytes, the host must initiate the Gecko menu.
  flash.poll(uart, 0);
  assert(uart.tx.size() == 1 && uart.tx[0] == '\r');
  flash.poll(uart, 2000);
  assert(uart.tx.size() == 1);  // No excessive menu request traffic.
  flash.poll(uart, 3000);
  assert(uart.tx.size() == 2 && uart.tx[1] == '\r');
  uart.inject("Gecko Bootloader\r\nBL >");
  flash.poll(uart, 3010);
  flash.poll(uart, 3020);
  assert(!uart.tx.empty() && uart.tx.back() == '1');
  uart.inject('C');
  flash.poll(uart, 3030);
  flash.poll(uart, 3040);
  assert(uart.tx.size() == 136);  // Two CRs + menu choice + 133-byte XMODEM frame.
  assert(uart.tx[3] == 0x01);     // SOH
  assert(uart.tx[4] == 1);        // Block 1
  assert(uart.tx[5] == 0xFE);     // Complement
  assert(uart.tx[6] == 1 && uart.tx[7] == 2 && uart.tx[8] == 3);
  assert(uart.tx[9] == 0x1a);  // Padding
  uart.inject(0x06);           // ACK block
  flash.poll(uart, 3050);
  assert(flash.percent() == 100);
  flash.poll(uart, 3060);
  assert(uart.tx.back() == 0x04);  // EOT
  uart.inject(0x06);
  flash.poll(uart, 3070);
  uart.inject("Serial upload complete\r\nBL >");
  flash.poll(uart, 3080);
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
  assert(timed_out.error() == "Bootloader menu timed out (no BL > prompt)");

  firmware::GeckoXmodem xmodem_timeout;
  MockUart silent_uart;
  assert(xmodem_timeout.begin(&image, 0));
  silent_uart.inject("Gecko Bootloader\r\nBL >");
  xmodem_timeout.poll(silent_uart, 10);
  xmodem_timeout.poll(silent_uart, 20010);
  assert(xmodem_timeout.failed());
  assert(xmodem_timeout.error() == "Bootloader XMODEM start timed out (no C)");

  // A first CR can be lost during reset. A later request must still be able
  // to trigger the bootloader prompt and start a transfer.
  firmware::GeckoXmodem delayed_menu;
  MockUart delayed_uart;
  assert(delayed_menu.begin(&image, 0));
  delayed_menu.poll(delayed_uart, 100);
  delayed_menu.poll(delayed_uart, 3100);
  assert(delayed_uart.tx.size() == 2);
  delayed_uart.inject("BL >");
  delayed_menu.poll(delayed_uart, 3150);
  delayed_menu.poll(delayed_uart, 3160);
  assert(delayed_uart.tx.back() == '1');
}
