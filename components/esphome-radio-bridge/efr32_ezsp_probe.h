#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "radio_identification.h"

namespace esphome::esphome_radio_bridge::identification {

// Non-blocking ASH/EZSP identification for EFR32 Zigbee NCPs. No ESP-IDF
// dependency: byte-level protocol is exercised by host C++ unit tests.
// Identification resets the ASH link; the bridge must keep TCP clients out
// and reset the radio again before restoring exclusive UART ownership to ZHA.
class Efr32EzspProbe final : public RadioProbe {
 public:
  ProbeState begin(ProbeUart& uart, uint32_t now_ms) override {
    identity_ = {};
    identity_.status = "Waiting for radio startup";
    length_ = 0;
    escaped_ = false;
    tx_seq_ = 0;
    rx_seq_ = 0;
    uart.flush_input();
    // Normal-mode GPIO reset precedes this call. Give the NCP time to boot
    // before sending ASH RST, otherwise it can silently discard our first frame.
    phase_ = Phase::SETTLING;
    entered_ms_ = now_ms;
    return ProbeState::RUNNING;
  }

  ProbeState poll(ProbeUart& uart, uint32_t now_ms) override {
    if (phase_ == Phase::DONE) return ProbeState::COMPLETE;
    if (phase_ == Phase::FAILED) return ProbeState::FAILED;
    if (phase_ == Phase::IDLE) return ProbeState::UNSUPPORTED;

    if (phase_ == Phase::SETTLING) {
      if (static_cast<uint32_t>(now_ms - entered_ms_) < 1500) return ProbeState::RUNNING;
      uart.flush_input();
      constexpr uint8_t rst[] = {0x1A, 0xC0, 0x38, 0xBC, 0x7E};
      if (uart.write(rst, sizeof(rst)) != static_cast<int>(sizeof(rst)))
        return fail_("ASH reset write failed");
      identity_.status = "Probing ASH";
      transition_(Phase::RSTACK, now_ms);
      return ProbeState::RUNNING;
    }

    // Bound work per ESPHome loop invocation. The UART read is non-blocking.
    uint8_t incoming[64];
    for (unsigned batch = 0; batch < 4 && phase_ != Phase::DONE && phase_ != Phase::FAILED; ++batch) {
      const int count = uart.read(incoming, sizeof(incoming));
      if (count < 0) return fail_("UART read failed");
      if (count == 0) break;
      for (int i = 0; i < count && phase_ != Phase::DONE && phase_ != Phase::FAILED; ++i)
        consume_(uart, incoming[i], now_ms);
    }

    if (phase_ != Phase::DONE && phase_ != Phase::FAILED &&
        static_cast<uint32_t>(now_ms - entered_ms_) >= phase_timeout_()) {
      if (identity_.responding) {
        // A positive ASH handshake is meaningful even when EZSP queries fail.
        return complete_(identity_.firmware == "Unknown" ? "Responding; version unavailable" : "Identified");
      }
      return fail_("No ASH RSTACK response");
    }
    return phase_ == Phase::DONE     ? ProbeState::COMPLETE
           : phase_ == Phase::FAILED ? ProbeState::FAILED
                                     : ProbeState::RUNNING;
  }

  void cancel() override {
    phase_ = Phase::IDLE;
    identity_.status = "Cancelled";
  }

  const RadioIdentity& identity() const override { return identity_; }

 private:
  enum class Phase { IDLE, SETTLING, RSTACK, LEGACY_VERSION, VERSION, VERSION_DETAIL, DONE, FAILED };
  static constexpr uint8_t FLAG = 0x7E;
  static constexpr uint8_t ESC = 0x7D;
  static constexpr size_t MAX_FRAME = 192;

  uint32_t phase_timeout_() const { return phase_ == Phase::RSTACK ? 3000 : 2000; }
  ProbeState fail_(const char* why) {
    phase_ = Phase::FAILED;
    identity_.status = why;
    return ProbeState::FAILED;
  }
  ProbeState complete_(const char* status) {
    phase_ = Phase::DONE;
    identity_.status = status;
    return ProbeState::COMPLETE;
  }
  void transition_(Phase phase, uint32_t now_ms) {
    phase_ = phase;
    entered_ms_ = now_ms;
  }

  static uint16_t crc16_(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
      crc ^= static_cast<uint16_t>(data[i]) << 8;
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                  : static_cast<uint16_t>(crc << 1);
    }
    return crc;
  }
  static bool reserved_(uint8_t value) {
    return value == FLAG || value == ESC || value == 0x11 || value == 0x13 || value == 0x18 || value == 0x1A;
  }
  static void whiten_(uint8_t* data, size_t len) {
    uint8_t rnd = 0x42;
    for (size_t i = 0; i < len; ++i) {
      data[i] ^= rnd;
      rnd = (rnd & 1U) != 0 ? static_cast<uint8_t>((rnd >> 1) ^ 0xB8) : static_cast<uint8_t>(rnd >> 1);
    }
  }
  bool send_frame_(ProbeUart& uart, const uint8_t* body, size_t len) {
    if (len == 0 || len > 80) return false;
    std::array<uint8_t, MAX_FRAME> escaped{};
    size_t out = 0;
    const uint16_t crc = crc16_(body, len);
    for (size_t i = 0; i < len + 2; ++i) {
      const uint8_t value = i < len    ? body[i]
                            : i == len ? static_cast<uint8_t>(crc >> 8)
                                       : static_cast<uint8_t>(crc);
      if (reserved_(value)) {
        escaped[out++] = ESC;
        escaped[out++] = static_cast<uint8_t>(value ^ 0x20);
      } else {
        escaped[out++] = value;
      }
    }
    escaped[out++] = FLAG;
    return uart.write(escaped.data(), out) == static_cast<int>(out);
  }
  bool send_ack_(ProbeUart& uart) {
    const uint8_t ack = static_cast<uint8_t>(0x80 | (rx_seq_ & 7));
    return send_frame_(uart, &ack, 1);
  }
  bool send_data_(ProbeUart& uart, const uint8_t* payload, size_t len) {
    std::array<uint8_t, 80> body{};
    if (len + 1 > body.size()) return false;
    body[0] = static_cast<uint8_t>(((tx_seq_ & 7) << 4) | (rx_seq_ & 7));
    for (size_t i = 0; i < len; ++i) body[i + 1] = payload[i];
    whiten_(body.data() + 1, len);
    if (!send_frame_(uart, body.data(), len + 1)) return false;
    tx_seq_ = static_cast<uint8_t>((tx_seq_ + 1) & 7);
    return true;
  }

  void consume_(ProbeUart& uart, uint8_t b, uint32_t now_ms) {
    if (b == 0x1A) {
      // ASH CANCEL abandons any incomplete frame.
      length_ = 0;
      escaped_ = false;
      return;
    }
    if (b == FLAG) {
      if (length_ >= 3) handle_frame_(uart, buffer_.data(), length_, now_ms);
      length_ = 0;
      escaped_ = false;
      return;
    }
    if (escaped_) {
      b ^= 0x20;
      escaped_ = false;
    } else if (b == ESC) {
      escaped_ = true;
      return;
    } else if (b == 0x11 || b == 0x13 || b == 0x18) {
      return;  // ASH serial control bytes, not frame data.
    }
    if (length_ < buffer_.size())
      buffer_[length_++] = b;
    else {
      length_ = 0;  // Discard oversized/malformed frame without heap allocations.
      escaped_ = false;
    }
  }

  void handle_frame_(ProbeUart& uart, const uint8_t* frame, size_t len, uint32_t now_ms) {
    if (len < 3) return;
    const uint16_t received = static_cast<uint16_t>(frame[len - 2] << 8) | frame[len - 1];
    if (crc16_(frame, len - 2) != received) return;
    const uint8_t control = frame[0];
    if (phase_ == Phase::RSTACK) {
      if (control != 0xC1 || len < 5 || frame[1] != 0x02) return;
      identity_.responding = true;
      if (!send_ack_(uart)) {
        fail_("ASH acknowledgement failed");
        return;
      }
      const uint8_t legacy[] = {0x00, 0x00, 0x00, 0x04};
      if (!send_data_(uart, legacy, sizeof(legacy))) {
        fail_("EZSP legacy version write failed");
        return;
      }
      identity_.status = "Querying EZSP";
      transition_(Phase::LEGACY_VERSION, now_ms);
      return;
    }
    if ((control & 0x80U) != 0 || len < 6) return;  // Only ASH DATA carries EZSP replies.
    const uint8_t ncp_seq = static_cast<uint8_t>((control >> 4) & 7);
    if (ncp_seq != rx_seq_) {
      send_ack_(uart);  // Ignore duplicate/out-of-order response.
      return;
    }
    rx_seq_ = static_cast<uint8_t>((rx_seq_ + 1) & 7);
    if (!send_ack_(uart)) {
      fail_("ASH acknowledgement failed");
      return;
    }
    std::array<uint8_t, MAX_FRAME> ezsp{};
    const size_t payload_len = len - 3;
    for (size_t i = 0; i < payload_len; ++i) ezsp[i] = frame[i + 1];
    whiten_(ezsp.data(), payload_len);

    if (phase_ == Phase::LEGACY_VERSION) {
      if (payload_len < 5 || ezsp[0] != 0 || ezsp[1] != 0x80 || ezsp[2] != 0x00) return;
      // Negotiate the extended EZSP frame format for protocol v8+.
      const uint8_t version[] = {0x01, 0x00, 0x01, 0x00, 0x00, 0x0E};
      if (!send_data_(uart, version, sizeof(version))) {
        fail_("EZSP version write failed");
        return;
      }
      transition_(Phase::VERSION, now_ms);
      return;
    }
    if (phase_ == Phase::VERSION) {
      if (payload_len < 9 || ezsp[0] != 1 || ezsp[1] != 0x80 || ezsp[2] != 0x01 || ezsp[3] != 0x00 ||
          ezsp[4] != 0x00)
        return;
      identity_.protocol_version = ezsp[5];
      const uint16_t stack = static_cast<uint16_t>(ezsp[7] | (uint16_t(ezsp[8]) << 8));
      char version[32];
      std::snprintf(version, sizeof(version), "%u.%u.%u.%u", (stack >> 12) & 15, (stack >> 8) & 15,
                    (stack >> 4) & 15, stack & 15);
      identity_.firmware = version;
      // Query GET_VALUE(VERSION_INFO) to report the detailed firmware release.
      const uint8_t detail[] = {0x02, 0x00, 0x01, 0xAA, 0x00, 0x11};
      if (!send_data_(uart, detail, sizeof(detail))) {
        complete_("Identified");
        return;
      }
      transition_(Phase::VERSION_DETAIL, now_ms);
      return;
    }
    if (phase_ == Phase::VERSION_DETAIL) {
      if (payload_len < 13 || ezsp[0] != 2 || ezsp[1] != 0x80 || ezsp[2] != 0x01 || ezsp[3] != 0xAA ||
          ezsp[4] != 0x00)
        return;
      const size_t status_len = identity_.protocol_version >= 14 ? 4 : 1;
      const size_t pos = 5 + status_len;
      if (payload_len < pos + 7) return;
      for (size_t i = 5; i < pos; ++i)
        if (ezsp[i] != 0) return;
      if (ezsp[pos] < 6 || payload_len < pos + 1 + ezsp[pos]) return;
      const size_t v = pos + 1;
      char version[40];
      const uint16_t build = static_cast<uint16_t>(ezsp[v] | (uint16_t(ezsp[v + 1]) << 8));
      std::snprintf(version, sizeof(version), "%u.%u.%u.%u (build %u)", ezsp[v + 2], ezsp[v + 3], ezsp[v + 4],
                    ezsp[v + 5], build);
      identity_.firmware = version;
      complete_("Identified");
    }
  }

  RadioIdentity identity_{};
  Phase phase_{Phase::IDLE};
  uint32_t entered_ms_{0};
  uint8_t tx_seq_{0};
  uint8_t rx_seq_{0};
  std::array<uint8_t, MAX_FRAME> buffer_{};
  size_t length_{0};
  bool escaped_{false};
};

}  // namespace esphome::esphome_radio_bridge::identification
