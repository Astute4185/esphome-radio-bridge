#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "flash_interface.h"

namespace esphome::esphome_radio_bridge::firmware {

// Host-testable, non-blocking Gecko UART XMODEM-CRC sender.  The bridge alone
// owns the UART while it is running; no ESPHome or ESP-IDF dependency here.
class GeckoXmodem final : public FlashProtocol {
 public:
  enum class State : uint8_t {
    IDLE,
    WAIT_MENU,
    WAIT_C,
    SEND_BLOCK,
    WAIT_ACK,
    SEND_EOT,
    WAIT_EOT_ACK,
    WAIT_COMPLETE,
    COMPLETE,
    FAILED,
  };

  bool begin(const FirmwareImage* image, uint32_t now_ms) override {
    if (image == nullptr || image->size() == 0 || image->size() > 2U * 1024U * 1024U || running())
      return false;
    image_ = image;
    image_size_ = image->size();
    block_index_ = 0;
    attempts_ = 0;
    tx_offset_ = 0;
    menu_progress_ = 0;
    pending_menu_choice_ = false;
    completion_text_.clear();
    error_.clear();
    state_ = State::WAIT_MENU;
    started_ms_ = now_ms;
    return true;
  }

  bool running() const override {
    return state_ != State::IDLE && state_ != State::COMPLETE && state_ != State::FAILED;
  }
  bool succeeded() const override { return state_ == State::COMPLETE; }
  bool failed() const override { return state_ == State::FAILED; }
  State state() const { return state_; }
  const std::string& error() const override { return error_; }
  unsigned percent() const override {
    const size_t total = (image_size_ + 127) / 128;
    return total == 0 ? 0 : static_cast<unsigned>(100 * block_index_ / total);
  }
  void cancel() override { fail_("Cancelled"); }

  void poll(identification::ProbeUart& uart, uint32_t now_ms) override {
    if (!running()) return;
    // Gecko's menu must receive command '1' before it can request XMODEM blocks.
    if (pending_menu_choice_ && state_ == State::WAIT_C) {
      const uint8_t choice = '1';
      const int sent = uart.write(&choice, 1);
      if (sent < 0) {
        fail_("Bootloader menu command failed");
        return;
      }
      if (sent == 0) return;
      pending_menu_choice_ = false;
    }
    uint8_t received[256];
    if (state_ == State::WAIT_MENU || state_ == State::WAIT_C || state_ == State::WAIT_ACK ||
        state_ == State::WAIT_EOT_ACK || state_ == State::WAIT_COMPLETE) {
      const int count = uart.read(received, sizeof(received));
      if (count < 0) {
        fail_("UART read failed");
        return;
      }
      for (int i = 0; i < count; ++i) {
        if (received[i] == 0x18) {
          fail_("Bootloader cancelled transfer");
          return;
        }
        if (receive_(received[i], now_ms)) return;
      }
    }
    if (!running()) return;
    const uint32_t timeout = timeout_ms_();
    if (static_cast<uint32_t>(now_ms - started_ms_) >= timeout) {
      if (state_ == State::WAIT_ACK || state_ == State::WAIT_EOT_ACK)
        retry_(now_ms);
      else
        fail_("Bootloader response timed out");
      return;
    }
    if (state_ == State::SEND_BLOCK || state_ == State::SEND_EOT) {
      if (state_ == State::SEND_BLOCK && tx_offset_ == 0 && !encode_frame_()) {
        fail_("Firmware staging read failed");
        return;
      }
      const size_t remaining = (state_ == State::SEND_EOT ? 1 : sizeof(frame_)) - tx_offset_;
      const uint8_t* data = state_ == State::SEND_EOT ? &eot_ : frame_;
      const int sent = uart.write(data + tx_offset_, remaining);
      if (sent < 0 || static_cast<size_t>(sent) > remaining) {
        fail_("UART write failed");
        return;
      }
      tx_offset_ += static_cast<size_t>(sent);
      if (tx_offset_ == (state_ == State::SEND_EOT ? 1U : sizeof(frame_))) {
        state_ = state_ == State::SEND_EOT ? State::WAIT_EOT_ACK : State::WAIT_ACK;
        started_ms_ = now_ms;
      }
    }
  }

 private:
  static constexpr uint8_t eot_ = 0x04;
  static constexpr uint8_t ack_ = 0x06;
  static constexpr uint8_t nak_ = 0x15;
  static constexpr unsigned MAX_ATTEMPTS = 10;
  static uint16_t crc_(const uint8_t* block) {
    uint16_t crc = 0;
    for (size_t i = 0; i < 128; ++i) {
      crc ^= static_cast<uint16_t>(block[i]) << 8;
      for (int b = 0; b < 8; ++b) crc = static_cast<uint16_t>((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
  }
  bool encode_frame_() {
    frame_[0] = 0x01;
    const uint8_t n = static_cast<uint8_t>(block_index_ + 1);
    frame_[1] = n;
    frame_[2] = n ^ 0xFF;
    const size_t offset = block_index_ * 128;
    const size_t size = std::min(static_cast<size_t>(128), image_size_ - offset);
    std::memset(frame_ + 3, 0x1A, 128);
    if (!image_->read(offset, frame_ + 3, size)) return false;
    const uint16_t crc = crc_(frame_ + 3);
    frame_[131] = static_cast<uint8_t>(crc >> 8);
    frame_[132] = static_cast<uint8_t>(crc);
    return true;
  }
  void transition_(State next, uint32_t now_ms) {
    state_ = next;
    started_ms_ = now_ms;
    tx_offset_ = 0;
  }
  void fail_(const char* error) {
    error_ = error;
    state_ = State::FAILED;
  }
  uint32_t timeout_ms_() const {
    switch (state_) {
      case State::WAIT_MENU:
        return 15000;
      case State::WAIT_C:
        return 20000;
      case State::WAIT_ACK:
        return 3000;
      case State::WAIT_EOT_ACK:
        return 5000;
      case State::WAIT_COMPLETE:
        return 60000;
      default:
        return 3000;
    }
  }
  void retry_(uint32_t now_ms) {
    if (++attempts_ >= MAX_ATTEMPTS) {
      fail_("XMODEM retries exhausted");
      return;
    }
    transition_(state_ == State::WAIT_EOT_ACK ? State::SEND_EOT : State::SEND_BLOCK, now_ms);
  }
  // Returns true when processing should stop for this poll (stage change).
  bool receive_(uint8_t byte, uint32_t now_ms) {
    switch (state_) {
      case State::WAIT_MENU: {
        constexpr char PROMPT[] = "BL >";
        menu_progress_ = byte == static_cast<uint8_t>(PROMPT[menu_progress_])
                             ? menu_progress_ + 1
                             : (byte == static_cast<uint8_t>(PROMPT[0]) ? 1 : 0);
        if (menu_progress_ == 4) {
          // The menu command is a single byte; send it on the next poll.
          transition_(State::WAIT_C, now_ms);
          pending_menu_choice_ = true;
          menu_progress_ = 0;
          return true;
        }
        return false;
      }
      case State::WAIT_C:
        if (byte == 'C') {
          attempts_ = 0;
          transition_(State::SEND_BLOCK, now_ms);
          return true;
        }
        return false;
      case State::WAIT_ACK:
        if (byte == ack_) {
          ++block_index_;
          attempts_ = 0;
          transition_(block_index_ * 128 >= image_size_ ? State::SEND_EOT : State::SEND_BLOCK, now_ms);
          return true;
        }
        if (byte == nak_) {
          retry_(now_ms);
          return true;
        }
        return false;
      case State::WAIT_EOT_ACK:
        if (byte == ack_) {
          completion_text_.clear();
          transition_(State::WAIT_COMPLETE, now_ms);
          return true;
        }
        if (byte == nak_) {
          retry_(now_ms);
          return true;
        }
        return false;
      case State::WAIT_COMPLETE:
        if (byte >= 32 && byte <= 126)
          completion_text_ += static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + 32 : byte);
        else
          completion_text_ += ' ';
        if (completion_text_.size() > 256) completion_text_.erase(0, completion_text_.size() - 200);
        if (completion_text_.find("bl >") != std::string::npos) {
          if (completion_text_.find("serial upload complete") != std::string::npos)
            state_ = State::COMPLETE;
          else
            fail_("Bootloader did not confirm GBL installation");
          return true;
        }
        return false;
      default:
        return false;
    }
  }

 private:
  const FirmwareImage* image_{nullptr};
  size_t image_size_{0};
  size_t block_index_{0};
  uint8_t frame_[133]{};
  size_t tx_offset_{0};
  uint8_t menu_progress_{0};
  bool pending_menu_choice_{false};
  unsigned attempts_{0};
  uint32_t started_ms_{0};
  State state_{State::IDLE};
  std::string error_;
  std::string completion_text_;
};

}  // namespace esphome::esphome_radio_bridge::firmware
