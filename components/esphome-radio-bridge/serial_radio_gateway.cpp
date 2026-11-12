#include "serial_radio_gateway.h"

#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"

namespace esphome::serial_radio_gateway {

static const char* const TAG = "serial_radio_gateway";

void SerialRadioGateway::setup() {
  if (driver_ == nullptr || !drivers::valid_profile(driver_->profile())) {
    this->report_error_("No valid board driver configured");
    return;
  }
  const auto& board = driver_->profile();
  if (!driver_->setup()) {
    this->report_error_("Board control GPIO setup failed");
    return;
  }
  if (!uart_.start(board.uart, board.normal_baud, board.normal_uart_mode, board.bootloader_uart_mode)) {
    this->report_error_("UART driver setup or mode initialisation failed");
    return;
  }
  mode_ = Mode::NORMAL;
  // Boot into application mode after ESP32 reboot or interrupted maintenance.
  if (!driver_->exit_bootloader()) {
    this->report_error_("Radio application reset failed");
    return;
  }
  if (connected_sensor_ != nullptr) connected_sensor_->publish_state(false);
  if (disconnects_sensor_ != nullptr) disconnects_sensor_->publish_state(0);
  this->publish_mode_();
}

void SerialRadioGateway::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP32 serial radio gateway:");
  if (driver_ == nullptr) {
    ESP_LOGE(TAG, "  Missing board driver");
    return;
  }
  const auto& board = driver_->profile();
  ESP_LOGCONFIG(TAG, "  Board driver: %s", board.id);
  ESP_LOGCONFIG(TAG, "  TCP port: %u", tcp_port_);
  ESP_LOGCONFIG(TAG, "  UART: UART%d, TX=%d RX=%d RTS=%d CTS=%d", board.uart.port, board.uart.tx,
                board.uart.rx, board.uart.rts, board.uart.cts);
  ESP_LOGCONFIG(TAG, "  Normal UART: %s", uart_modes::mode_name(board.normal_uart_mode));
  ESP_LOGCONFIG(TAG, "  Bootloader UART: %s", uart_modes::mode_name(board.bootloader_uart_mode));
  ESP_LOGCONFIG(TAG, "  Reset GPIO: %d", board.reset_gpio);
  ESP_LOGCONFIG(TAG, "  Boot GPIO: %d", board.boot_gpio);
  if (this->is_failed()) ESP_LOGE(TAG, "Component setup failed");
}

void SerialRadioGateway::report_error_(const char* message) {
  ESP_LOGE(TAG, "%s", message);
  error_ = true;
  this->status_set_error();
  this->mark_failed();
}

bool SerialRadioGateway::configure_uart_(Mode new_mode) {
  if (!uart_.ready() || driver_ == nullptr) return false;
  const auto& board = driver_->profile();
  const uint32_t baud = new_mode == Mode::NORMAL ? board.normal_baud : board.bootloader_baud;
  const auto wire_mode = new_mode == Mode::NORMAL ? board.normal_uart_mode : board.bootloader_uart_mode;
  // Always apply the mode; normal and bootloader speeds may be identical.
  if (!uart_.configure(baud, wire_mode)) {
    ESP_LOGE(TAG, "Unable to configure UART for requested mode");
    return false;
  }
  uart_to_tcp_.clear();
  tcp_to_uart_.clear();
  mode_ = new_mode;
  this->publish_mode_();
  return true;
}

void SerialRadioGateway::radio_reset() {
  if (!allowed(mode_, Command::RESET, armed_, client_fd_ >= 0)) {
    ESP_LOGW(TAG, "Reset denied: TCP client connected or maintenance active");
    return;
  }
  if (!driver_->reset_radio()) this->report_error_("Radio reset failed");
}

void SerialRadioGateway::enter_bootloader() {
  if (!allowed(mode_, Command::ENTER_BOOTLOADER, armed_, client_fd_ >= 0) ||
      !driver_->profile().supports_bootloader) {
    ESP_LOGW(TAG, "Bootloader denied: disconnect ZHA and arm maintenance first");
    return;
  }
  if (!this->configure_uart_(Mode::BOOTLOADER)) {
    if (!this->configure_uart_(Mode::NORMAL))
      this->report_error_("RTS/CTS recovery failed after bootloader UART error");
    return;
  }
  if (!driver_->enter_bootloader()) {
    const bool restarted = driver_->exit_bootloader();
    const bool restored = this->configure_uart_(Mode::NORMAL);
    if (!restarted || !restored) this->report_error_("Radio recovery failed after bootloader entry error");
    return;
  }
  last_maintenance_activity_ = millis();
  ESP_LOGI(TAG, "Radio bootloader entered; board-specific UART mode active");
}

void SerialRadioGateway::exit_bootloader() {
  if (!allowed(mode_, Command::EXIT_BOOTLOADER, armed_, client_fd_ >= 0)) {
    ESP_LOGW(TAG, "Exit bootloader denied while flashing TCP client is connected");
    return;
  }
  if (!driver_->exit_bootloader() || !this->configure_uart_(Mode::NORMAL)) {
    this->report_error_("Radio application reset or RTS/CTS restoration failed");
    return;
  }
  armed_ = false;
  if (armed_switch_ != nullptr) armed_switch_->turn_off();
  ESP_LOGI(TAG, "Radio application reset and RTS/CTS restored");
}

void SerialRadioGateway::publish_mode_() {
  if (mode_sensor_ == nullptr || driver_ == nullptr) return;
  const auto& board = driver_->profile();
  const auto wire_mode = mode_ == Mode::NORMAL ? board.normal_uart_mode : board.bootloader_uart_mode;
  mode_sensor_->publish_state(std::string(mode_ == Mode::NORMAL ? "Normal (" : "Bootloader (") +
                              uart_modes::mode_name(wire_mode) + ")");
}

void SerialRadioGateway::mark_maintenance_activity_() {
  if (mode_ == Mode::BOOTLOADER) last_maintenance_activity_ = millis();
}

void SerialRadioGateway::open_listener_() {
  listener_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener_fd_ < 0) return;
  int reuse = 1;
  setsockopt(listener_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(tcp_port_);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(listener_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listener_fd_, 1) != 0 || fcntl(listener_fd_, F_SETFL, O_NONBLOCK) != 0) {
    ESP_LOGW(TAG, "Cannot listen on TCP port %u: errno %d", tcp_port_, errno);
    this->close_listener_();
  }
}

void SerialRadioGateway::close_listener_() {
  if (listener_fd_ >= 0) {
    ::close(listener_fd_);
    listener_fd_ = -1;
  }
}

void SerialRadioGateway::close_client_() {
  if (client_fd_ < 0) return;
  ::close(client_fd_);
  client_fd_ = -1;
  ++disconnects_;
  if (disconnects_sensor_ != nullptr) disconnects_sensor_->publish_state(disconnects_);
  if (connected_sensor_ != nullptr) connected_sensor_->publish_state(false);
  tcp_to_uart_.clear();
  // Keep bootloader greeting cached while waiting for a host flasher.
  if (mode_ == Mode::NORMAL) uart_to_tcp_.clear();
}

void SerialRadioGateway::accept_client_() {
  if (listener_fd_ < 0) return;
  int incoming = ::accept(listener_fd_, nullptr, nullptr);
  if (incoming < 0) return;
  if (client_fd_ >= 0) {
    ::close(incoming);
    return;
  }
  if (fcntl(incoming, F_SETFL, O_NONBLOCK) != 0) {
    ::close(incoming);
    return;
  }
  int nodelay = 1;
  setsockopt(incoming, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
  client_fd_ = incoming;
  if (mode_ == Mode::NORMAL) {
    // Avoid forwarding application data buffered before the ZHA connection.
    uart_.flush_input();
    uart_to_tcp_.clear();
  }
  if (connected_sensor_ != nullptr) connected_sensor_->publish_state(true);
  this->mark_maintenance_activity_();
}

void SerialRadioGateway::bridge_uart_() {
  if (uart_to_tcp_.free() == 0) return;
  uint8_t incoming[256];
  size_t length = std::min(sizeof(incoming), uart_to_tcp_.free());
  int count = uart_.read(incoming, length);
  if (count <= 0) return;
  if (client_fd_ >= 0 || mode_ == Mode::BOOTLOADER) {
    uart_to_tcp_.push(incoming, count);
  }
}

void SerialRadioGateway::bridge_tcp_() {
  if (client_fd_ < 0 || tcp_to_uart_.free() == 0) return;
  uint8_t incoming[256];
  size_t count = std::min(sizeof(incoming), tcp_to_uart_.free());
  int got = ::recv(client_fd_, incoming, count, 0);
  if (got == 0) {
    this->close_client_();
    return;
  }
  if (got < 0) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) this->close_client_();
    return;
  }
  tcp_to_uart_.push(incoming, got);
  this->mark_maintenance_activity_();
}

void SerialRadioGateway::send_tcp_() {
  if (client_fd_ < 0 || uart_to_tcp_.empty()) return;
  int sent = ::send(client_fd_, uart_to_tcp_.head_data(), uart_to_tcp_.head_size(), 0);
  if (sent > 0) {
    uart_to_tcp_.consume(sent);
  } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    this->close_client_();
  }
}

void SerialRadioGateway::loop() {
  if (!uart_.ready() || error_
    return;
  if (!network::is_connected()) {
    this->close_client_();
    this->close_listener_();
  } else {
    if (listener_fd_ < 0) this->open_listener_();
    if (client_fd_ < 0) this->accept_client_();
  }
  if (!tcp_to_uart_.empty()) {
    int accepted = uart_.write(tcp_to_uart_.head_data(), std::min<size_t>(tcp_to_uart_.head_size(), 128));
    if (accepted > 0) tcp_to_uart_.consume(accepted);
  }
  this->bridge_tcp_();
  this->bridge_uart_();
  this->send_tcp_();
  if (mode_ == Mode::BOOTLOADER &&
      static_cast<uint32_t>(millis() - last_maintenance_activity_) > maintenance_timeout_ms_) {
    ESP_LOGW(TAG, "Bootloader idle timeout: returning radio to normal mode");
    this->close_client_();
    this->exit_bootloader();
  }
}

void SerialRadioGateway::on_shutdown() {
  this->close_client_();
  this->close_listener_();
  // Release bootloader select and UART ownership during ESP32 shutdown.
  if (driver_ != nullptr) driver_->shutdown();
  uart_.stop();
}

}  // namespace esphome::serial_radio_gateway
