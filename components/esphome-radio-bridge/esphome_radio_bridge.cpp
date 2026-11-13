#include "esphome_radio_bridge.h"

#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"

namespace esphome::esphome_radio_bridge {

static const char* const TAG = "esphome_radio_bridge";

std::string RadioBridge::effective_catalogue_url_() const {
  const char* driver_default = driver_ != nullptr ? driver_->default_catalogue_url() : nullptr;
  return firmware::select_catalogue_url(catalogue_url_, driver_default);
}

void RadioBridge::setup() {
  this->publish_bridge_health_(BridgeHealth::STARTING);
  if (manifest_override_text_ != nullptr) manifest_override_text_->restore_override();
  this->publish_catalogue_status_("Idle");
  this->publish_firmware_update_status_();
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
  normal_uart_experiment_ = board.normal_uart_mode == uart_modes::UartModeId::FOUR_WIRE
                                ? uart_modes::NormalUartExperiment::FOUR_WIRE_PROFILE
                                : uart_modes::NormalUartExperiment::TWO_WIRE;
  // Boot into application mode after ESP32 reboot or interrupted maintenance.
  if (!driver_->exit_bootloader()) {
    this->report_error_("Radio application reset failed");
    return;
  }
  if (connected_sensor_ != nullptr) connected_sensor_->publish_state(false);
  if (disconnects_sensor_ != nullptr) disconnects_sensor_->publish_state(0);
  if (transport_errors_sensor_ != nullptr) transport_errors_sensor_->publish_state(0);
  if (last_disconnect_reason_sensor_ != nullptr) last_disconnect_reason_sensor_->publish_state("None");
  this->publish_mode_();
  this->publish_uart_mode_select_();
  this->publish_uart_mode_status_("Ready: " +
                                  std::string(uart_modes::experiment_option(normal_uart_experiment_)));
  this->begin_radio_identification_();
  this->update_bridge_health_(network::is_connected());
}

void RadioBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "ESPHome radio bridge:");
  if (driver_ == nullptr) {
    ESP_LOGE(TAG, "  Missing board driver");
    return;
  }
  const auto& board = driver_->profile();
  ESP_LOGCONFIG(TAG, "  Board driver: %s", board.id);
  ESP_LOGCONFIG(TAG, "  TCP port: %u", tcp_port_);
  const auto catalogue_url = this->effective_catalogue_url_();
  ESP_LOGCONFIG(TAG, "  Catalogue URL: %s", catalogue_url.empty() ? "Not configured" : catalogue_url.c_str());
  ESP_LOGCONFIG(TAG, "  Catalogue source: %s",
                catalogue_url_.empty() ? "Board driver default" : "YAML override");
  ESP_LOGCONFIG(TAG, "  Firmware channel: %s", board.radio_type);
  ESP_LOGCONFIG(TAG, "  Flash protocol: %s",
                driver_->flash_protocol() != nullptr ? "Available" : "Unsupported");
  ESP_LOGCONFIG(TAG, "  UART: UART%d, TX=%d RX=%d RTS=%d CTS=%d", board.uart.port, board.uart.tx,
                board.uart.rx, board.uart.rts, board.uart.cts);
  ESP_LOGCONFIG(TAG, "  Normal UART: %s", uart_modes::mode_name(board.normal_uart_mode));
  ESP_LOGCONFIG(TAG, "  Bootloader UART: %s", uart_modes::mode_name(board.bootloader_uart_mode));
  ESP_LOGCONFIG(TAG, "  Runtime UART experiment: %s", uart_mode_select_ == nullptr ? "disabled" : "enabled");
  ESP_LOGCONFIG(TAG, "  Flow-control wiring verified: %s", flow_control_wiring_verified_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Reset GPIO: %d", board.reset_gpio);
  ESP_LOGCONFIG(TAG, "  Boot GPIO: %d", board.boot_gpio);
  ESP_LOGCONFIG(TAG, "  Radio identification: %s",
                driver_->identification_probe() == nullptr ? "Unsupported" : "At UART startup");
  if (this->is_failed()) ESP_LOGE(TAG, "Component setup failed");
}

void RadioBridge::report_error_(const char* message) {
  ESP_LOGE(TAG, "%s", message);
  error_ = true;
  last_failure_ = message;
  this->publish_bridge_health_(BridgeHealth::FAILED);
  this->status_set_error();
  this->mark_failed();
}

void RadioBridge::publish_radio_identity_(const identification::RadioIdentity& identity) {
  const bool version_changed = reported_firmware_ != identity.firmware;
  if (version_changed) {
    reported_firmware_ = identity.firmware;
    if (installed_firmware_sensor_ != nullptr) installed_firmware_sensor_->publish_state(identity.firmware);
  }
  const bool old_responding = installed_radio_responding_;
  installed_radio_responding_ = identity.responding;
  if (reported_identification_status_ != identity.status) {
    reported_identification_status_ = identity.status;
    if (radio_identification_status_sensor_ != nullptr)
      radio_identification_status_sensor_->publish_state(identity.status);
  }
  if (version_changed || old_responding != installed_radio_responding_) this->publish_update_assessment_();
}

void RadioBridge::begin_radio_identification_() {
  if (!uart_.ready() || driver_ == nullptr || identifying_ || client_fd_ >= 0 || mode_ != Mode::NORMAL)
    return;
  // All normal-mode UART startups pass through this driver-owned probe hook.
  // No listener may accept a client until the probe is finished or times out.
  this->close_listener_();
  uart_to_tcp_.clear();
  tcp_to_uart_.clear();
  active_probe_ = driver_->identification_probe();
  if (active_probe_ == nullptr) {
    this->publish_radio_identity_({"Unknown", "Unsupported", false, 0});
    return;
  }
  const auto state = active_probe_->begin(uart_, millis());
  identifying_ = state == identification::ProbeState::RUNNING;
  this->publish_radio_identity_(active_probe_->identity());
  if (!identifying_) {
    // Failure to identify is a diagnostic result, not failure of the TCP bridge.
    uart_.flush_input();
    active_probe_ = nullptr;
  }
}

void RadioBridge::poll_radio_identification_() {
  if (!identifying_ || active_probe_ == nullptr) return;
  const auto state = active_probe_->poll(uart_, millis());
  this->publish_radio_identity_(active_probe_->identity());
  if (state == identification::ProbeState::RUNNING) return;
  active_probe_ = nullptr;
  identifying_ = false;
  // An ASH/EZSP probe owns the radio's link state. Restore a fresh NCP session
  // before exposing raw UART bytes to ZHA/zigpy over TCP.
  if (!driver_->exit_bootloader()) {
    this->report_error_("Unable to reset radio after identification");
    return;
  }
  uart_.flush_input();
  uart_to_tcp_.clear();
  tcp_to_uart_.clear();
  listener_retry_pending_ = false;
  ESP_LOGI(TAG, "Radio identification finished, normal TCP UART access restored");
}

void RadioBridge::probe_radio() {
  if (error_ || !uart_.ready() || mode_ != Mode::NORMAL || armed_ || client_fd_ >= 0 || identifying_ ||
      flash_phase_ != FirmwareFlashPhase::IDLE) {
    ESP_LOGW(TAG, "Radio probe denied: TCP client, maintenance or another probe is active");
    reported_identification_status_ = "Blocked: client or maintenance active";
    if (radio_identification_status_sensor_ != nullptr)
      radio_identification_status_sensor_->publish_state(reported_identification_status_);
    return;
  }
  this->begin_radio_identification_();
  this->update_bridge_health_(network::is_connected());
}

bool RadioBridge::configure_uart_(Mode new_mode) {
  if (!uart_.ready() || driver_ == nullptr) return false;
  const auto& board = driver_->profile();
  const uint32_t baud = new_mode == Mode::NORMAL ? board.normal_baud : board.bootloader_baud;
  const auto wire_mode = new_mode == Mode::NORMAL ? uart_modes::experiment_wire_mode(normal_uart_experiment_)
                                                  : board.bootloader_uart_mode;
  const auto pins = new_mode == Mode::NORMAL ? this->normal_pinout_() : board.uart;
  // Always apply the mode; normal and bootloader speeds may be identical.
  if (!uart_.configure(baud, wire_mode, pins)) {
    ESP_LOGE(TAG, "Unable to configure UART for requested mode");
    return false;
  }
  uart_to_tcp_.clear();
  tcp_to_uart_.clear();
  mode_ = new_mode;
  this->publish_mode_();
  return true;
}

void RadioBridge::radio_reset() {
  if (driver_ == nullptr || error_ || identifying_ || flash_phase_ != FirmwareFlashPhase::IDLE ||
      !allowed(mode_, Command::RESET, armed_, client_fd_ >= 0)) {
    ESP_LOGW(TAG, "Reset denied: TCP client connected or maintenance active");
    return;
  }
  if (!driver_->reset_radio())
    this->report_error_("Radio reset failed");
  else
    this->begin_radio_identification_();
}

void RadioBridge::enter_bootloader() {
  if (driver_ == nullptr || error_ || identifying_ || flash_phase_ != FirmwareFlashPhase::IDLE ||
      !allowed(mode_, Command::ENTER_BOOTLOADER, armed_, client_fd_ >= 0) ||
      !driver_->profile().supports_bootloader) {
    ESP_LOGW(TAG, "Bootloader denied: disconnect ZHA and arm maintenance first");
    return;
  }
  if (!this->configure_uart_(Mode::BOOTLOADER)) {
    if (!this->configure_uart_(Mode::NORMAL))
      this->report_error_("UART mode recovery failed after bootloader UART error");
    return;
  }
  if (!driver_->enter_bootloader()) {
    const bool restarted = driver_->exit_bootloader();
    const bool restored = this->configure_uart_(Mode::NORMAL);
    if (!restarted || !restored) this->report_error_("Radio recovery failed after bootloader entry error");
    return;
  }
  last_maintenance_activity_ = millis();
  this->update_bridge_health_(network::is_connected());
  ESP_LOGI(TAG, "Radio bootloader entered; board-specific UART mode active");
}

void RadioBridge::exit_bootloader() {
  if (driver_ == nullptr || error_ || identifying_ || flash_phase_ != FirmwareFlashPhase::IDLE ||
      !allowed(mode_, Command::EXIT_BOOTLOADER, armed_, client_fd_ >= 0)) {
    ESP_LOGW(TAG, "Exit bootloader denied while flashing TCP client is connected");
    return;
  }
  if (!driver_->exit_bootloader() || !this->configure_uart_(Mode::NORMAL)) {
    this->report_error_("Radio application reset or UART mode restoration failed");
    return;
  }
  armed_ = false;
  if (armed_switch_ != nullptr) armed_switch_->turn_off();
  this->begin_radio_identification_();
  this->update_bridge_health_(network::is_connected());
  ESP_LOGI(TAG, "Radio application reset and normal UART mode restored");
}

uart_modes::UartPinout RadioBridge::normal_pinout_() const {
  return uart_modes::experiment_pinout(driver_->profile().uart, normal_uart_experiment_);
}

void RadioBridge::publish_uart_mode_select_() {
  if (uart_mode_select_ != nullptr)
    uart_mode_select_->publish_state(uart_modes::experiment_option(normal_uart_experiment_));
}

void RadioBridge::publish_uart_mode_status_(const std::string& status) {
  if (uart_mode_status_sensor_ != nullptr) uart_mode_status_sensor_->publish_state(status);
  ESP_LOGI(TAG, "UART experiment: %s", status.c_str());
}

bool RadioBridge::select_uart_mode(const std::string& option) {
  using uart_modes::NormalUartExperiment;
  NormalUartExperiment requested{};
  if (option == uart_modes::experiment_option(NormalUartExperiment::TWO_WIRE))
    requested = NormalUartExperiment::TWO_WIRE;
  else if (option == uart_modes::experiment_option(NormalUartExperiment::FOUR_WIRE_PROFILE))
    requested = NormalUartExperiment::FOUR_WIRE_PROFILE;
  else if (option == uart_modes::experiment_option(NormalUartExperiment::FOUR_WIRE_SWAPPED))
    requested = NormalUartExperiment::FOUR_WIRE_SWAPPED;
  else {
    this->publish_uart_mode_status_("Rejected: invalid UART mode");
    this->publish_uart_mode_select_();
    return false;
  }
  if (requested == normal_uart_experiment_) {
    this->publish_uart_mode_select_();
    return true;
  }
  if (!uart_modes::can_switch_normal_uart(mode_ == Mode::NORMAL, client_fd_ >= 0, identifying_,
                                          flash_phase_ != FirmwareFlashPhase::IDLE, armed_, error_) ||
      !uart_.ready()) {
    this->publish_uart_mode_status_("Blocked: disconnect ZHA and finish maintenance/probing first");
    this->publish_uart_mode_select_();
    return false;
  }
  if (requested != NormalUartExperiment::TWO_WIRE && !flow_control_wiring_verified_) {
    this->publish_uart_mode_status_("Blocked: verify RTS/CTS wiring before enabling 4-wire");
    this->publish_uart_mode_select_();
    return false;
  }
  const auto& board = driver_->profile();
  if (!uart_modes::experiment_supported(board.uart, board.bootloader_uart_mode, requested)) {
    this->publish_uart_mode_status_("Rejected: board does not support requested UART pins");
    this->publish_uart_mode_select_();
    return false;
  }

  // The UART is exclusively owned here; close the idle listener and reset the
  // NCP session after changing its flow-control wiring.
  this->close_listener_();
  const auto previous = normal_uart_experiment_;
  normal_uart_experiment_ = requested;
  if (!this->configure_uart_(Mode::NORMAL) || !driver_->exit_bootloader()) {
    normal_uart_experiment_ = previous;
    if (!this->configure_uart_(Mode::NORMAL) || !driver_->exit_bootloader()) {
      this->report_error_("UART experiment recovery failed");
    } else {
      this->publish_uart_mode_status_("Failed to apply mode; previous mode restored");
      this->begin_radio_identification_();
    }
    this->publish_uart_mode_select_();
    return false;
  }
  this->publish_uart_mode_select_();
  const auto pinout = this->normal_pinout_();
  this->publish_uart_mode_status_(std::string("Applied ") + uart_modes::experiment_option(requested) +
                                  " (ESP RTS GPIO" + std::to_string(pinout.rts) + " / CTS GPIO" +
                                  std::to_string(pinout.cts) + ")");
  this->begin_radio_identification_();
  this->update_bridge_health_(network::is_connected());
  return true;
}

void RadioBridge::publish_mode_() {
  if (mode_sensor_ == nullptr || driver_ == nullptr) return;
  const auto& board = driver_->profile();
  const auto wire_mode = mode_ == Mode::NORMAL ? uart_modes::experiment_wire_mode(normal_uart_experiment_)
                                               : board.bootloader_uart_mode;
  const auto pins = mode_ == Mode::NORMAL ? this->normal_pinout_() : board.uart;
  mode_sensor_->publish_state(
      std::string(mode_ == Mode::NORMAL ? "Normal (" : "Bootloader (") + uart_modes::mode_name(wire_mode) +
      (uart_modes::requires_rts_cts(wire_mode)
           ? ", RTS GPIO" + std::to_string(pins.rts) + " CTS GPIO" + std::to_string(pins.cts)
           : "") +
      ")");
}

void RadioBridge::mark_maintenance_activity_() {
  if (mode_ == Mode::BOOTLOADER) last_maintenance_activity_ = millis();
}

void RadioBridge::publish_bridge_health_(BridgeHealth health) {
  if (health_published_ && health == published_health_) return;
  const bool first_publish = !health_published_;
  published_health_ = health;
  health_published_ = true;
  if (bridge_ready_sensor_ != nullptr) {
    if (first_publish)
      bridge_ready_sensor_->publish_initial_state(bridge_is_ready(health));
    else
      bridge_ready_sensor_->publish_state(bridge_is_ready(health));
  }
  if (bridge_status_sensor_ != nullptr) {
    if (health == BridgeHealth::FAILED && last_failure_ != nullptr)
      bridge_status_sensor_->publish_state(std::string("Failed: ") + last_failure_);
    else
      bridge_status_sensor_->publish_state(bridge_health_name(health));
  }
}

void RadioBridge::update_bridge_health_(bool network_ready) {
  this->publish_bridge_health_(evaluate_bridge_health(
      uart_.ready(), error_, network_ready, listener_fd_ >= 0,
      mode_ == Mode::BOOTLOADER || flash_phase_ != FirmwareFlashPhase::IDLE, identifying_));
}

void RadioBridge::record_transport_error_() {
  ++transport_errors_;
  if (transport_errors_sensor_ != nullptr) transport_errors_sensor_->publish_state(transport_errors_);
}

void RadioBridge::open_listener_() {
  // A failed bind must not flood logs or retry on every ESPHome loop iteration.
  listener_retry_pending_ = true;
  last_listener_attempt_ms_ = millis();
  listener_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener_fd_ < 0) {
    ESP_LOGW(TAG, "Cannot create TCP listener on port %u: errno %d", tcp_port_, errno);
    this->record_transport_error_();
    return;
  }
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
    this->record_transport_error_();
  }
}

void RadioBridge::close_listener_() {
  if (listener_fd_ >= 0) {
    ::close(listener_fd_);
    listener_fd_ = -1;
  }
}

void RadioBridge::close_client_(DisconnectReason reason) {
  if (client_fd_ < 0) return;
  ::close(client_fd_);
  client_fd_ = -1;
  ++disconnects_;
  if (disconnects_sensor_ != nullptr) disconnects_sensor_->publish_state(disconnects_);
  if (connected_sensor_ != nullptr) connected_sensor_->publish_state(false);
  if (last_disconnect_reason_sensor_ != nullptr)
    last_disconnect_reason_sensor_->publish_state(disconnect_reason_name(reason));
  if (is_transport_error(reason)) this->record_transport_error_();
  ESP_LOGI(TAG, "TCP client disconnected: %s", disconnect_reason_name(reason));
  tcp_to_uart_.clear();
  // Keep bootloader greeting cached while waiting for a host flasher.
  if (mode_ == Mode::NORMAL) uart_to_tcp_.clear();
}

void RadioBridge::accept_client_() {
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

void RadioBridge::bridge_uart_() {
  if (uart_to_tcp_.free() == 0) return;
  uint8_t incoming[256];
  size_t length = std::min(sizeof(incoming), uart_to_tcp_.free());
  int count = uart_.read(incoming, length);
  if (count <= 0) return;
  if (client_fd_ >= 0 || mode_ == Mode::BOOTLOADER) {
    uart_to_tcp_.push(incoming, count);
  }
}

void RadioBridge::bridge_tcp_() {
  if (client_fd_ < 0 || tcp_to_uart_.free() == 0) return;
  uint8_t incoming[256];
  size_t count = std::min(sizeof(incoming), tcp_to_uart_.free());
  int got = ::recv(client_fd_, incoming, count, 0);
  if (got == 0) {
    this->close_client_(DisconnectReason::REMOTE_CLOSED);
    return;
  }
  if (got < 0) {
    if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
      this->close_client_(DisconnectReason::SOCKET_ERROR);
    return;
  }
  tcp_to_uart_.push(incoming, got);
  this->mark_maintenance_activity_();
}

void RadioBridge::send_tcp_() {
  if (client_fd_ < 0 || uart_to_tcp_.empty()) return;
  int sent = ::send(client_fd_, uart_to_tcp_.head_data(), uart_to_tcp_.head_size(), 0);
  if (sent > 0) {
    uart_to_tcp_.consume(sent);
  } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
    this->close_client_(DisconnectReason::SOCKET_ERROR);
  }
}

void RadioBridge::loop() {
  firmware::FirmwareLoadResult completed;
  if (catalogue_.collect(completed)) {
    if (catalogue_source_epoch_ == catalogue_request_epoch_) {
      catalogue_result_ = std::move(completed);
      last_update_check_succeeded_ = catalogue_result_.ok;
      last_update_check_ms_ = millis();  // Count the interval from completion, not start.
      this->publish_update_assessment_();
      this->publish_catalogue_status_(catalogue_result_.ok ? catalogue_result_.message
                                                           : "Error: " + catalogue_result_.message);
    } else {
      has_update_check_attempt_ = false;  // Discard an obsolete in-flight request.
      this->publish_catalogue_status_("Idle: source changed; refresh required");
    }
  }
  if (flash_phase_ == FirmwareFlashPhase::IDLE) this->check_for_updates_if_due_();
  if (!uart_.ready() || error_) return;
  if (flash_phase_ != FirmwareFlashPhase::IDLE) {
    this->poll_firmware_flash_();
    this->update_bridge_health_(network::is_connected());
    return;  // Flashing owns the UART and blocks new TCP client connections.
  }
  if (identifying_) {
    this->poll_radio_identification_();
    this->update_bridge_health_(network::is_connected());
    return;  // Exclusive UART ownership: TCP must never read probe responses.
  }
  const bool network_ready = network::is_connected();
  if (!network_ready) {
    this->close_client_(DisconnectReason::NETWORK_LOST);
    this->close_listener_();
    // Reopen immediately once the network returns.
    listener_retry_pending_ = false;
  } else {
    if (listener_fd_ < 0 &&
        listener_retry_due(listener_retry_pending_, millis(), last_listener_attempt_ms_, LISTENER_RETRY_MS))
      this->open_listener_();
    if (client_fd_ < 0) this->accept_client_();
  }
  this->update_bridge_health_(network_ready);
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
    this->close_client_(DisconnectReason::MAINTENANCE);
    this->exit_bootloader();
  }
}

void RadioBridge::publish_catalogue_status_(const std::string& status) {
  if (reported_catalogue_status_ == status) return;
  reported_catalogue_status_ = status;
  if (catalogue_status_sensor_ != nullptr) catalogue_status_sensor_->publish_state(status);
}

bool RadioBridge::set_manifest_override_url(const std::string& url) {
  if (!url.empty() && !firmware::is_https_url(url)) return false;
  if (manifest_override_url_ == url) return true;
  manifest_override_url_ = url;
  ++catalogue_source_epoch_;
  catalogue_result_ = {};
  has_update_check_attempt_ = false;  // Schedule an immediate check against the new source.
  this->clear_update_entities_();
  this->publish_catalogue_status_("Idle: source changed; refresh required");
  return true;
}

void RadioBridge::clear_update_entities_() {
  if (latest_firmware_sensor_ != nullptr && latest_firmware_sensor_->has_state())
    latest_firmware_sensor_->publish_state("Unknown");
  if (update_available_sensor_ != nullptr && update_available_sensor_->has_state())
    update_available_sensor_->invalidate_state();
}

void RadioBridge::publish_update_assessment_() {
  if (!catalogue_result_.ok) {
    this->clear_update_entities_();
    return;
  }
  if (latest_firmware_sensor_ != nullptr) {
    const std::string latest = firmware::latest_radio_version(catalogue_result_);
    if (!latest_firmware_sensor_->has_state() || latest_firmware_sensor_->state != latest)
      latest_firmware_sensor_->publish_state(latest);
  }
  const auto assessment =
      firmware::assess_update(catalogue_result_, reported_firmware_, installed_radio_responding_);
  if (update_available_sensor_ == nullptr) return;
  if (assessment.availability == firmware::UpdateAvailability::UNKNOWN) {
    if (update_available_sensor_->has_state()) update_available_sensor_->invalidate_state();
    ESP_LOGD(TAG, "Firmware update availability unknown: %s", assessment.reason);
    return;
  }
  const bool available = assessment.availability == firmware::UpdateAvailability::AVAILABLE;
  if (!update_available_sensor_->has_state())
    update_available_sensor_->publish_initial_state(available);
  else if (update_available_sensor_->state != available)
    update_available_sensor_->publish_state(available);
}

void RadioBridge::check_for_updates_if_due_() {
  // Network-only metadata checks must never take ownership of the radio UART.
  if (http_request_ == nullptr ||
      (this->effective_catalogue_url_().empty() && manifest_override_url_.empty()) ||
      !network::is_connected() || catalogue_.busy())
    return;
  if (firmware::update_check_due(has_update_check_attempt_, millis(), last_update_check_ms_,
                                 last_update_check_succeeded_))
    this->refresh_catalogue();
}

void RadioBridge::refresh_catalogue() {
  const auto catalogue_url = this->effective_catalogue_url_();
  if (driver_ == nullptr || http_request_ == nullptr ||
      (catalogue_url.empty() && manifest_override_url_.empty())) {
    this->publish_catalogue_status_("Error: catalogue not configured");
    return;
  }
  if (catalogue_.busy()) {
    this->publish_catalogue_status_("Loading");
    return;
  }
  if (!network::is_connected()) {
    this->publish_catalogue_status_("Error: network offline");
    return;
  }
  has_update_check_attempt_ = true;
  last_update_check_ms_ = millis();
  last_update_check_succeeded_ = false;  // A failed launch retries after the backoff.
  const auto& board = driver_->profile();
  if (!catalogue_.start(http_request_, catalogue_url, manifest_override_url_, board.id, board.radio_type)) {
    this->publish_catalogue_status_("Error: unable to start catalogue request");
    return;
  }
  catalogue_request_epoch_ = catalogue_source_epoch_;
  this->publish_catalogue_status_("Loading");
}

void ManifestOverrideText::restore_override() {
  pref_ = this->make_entity_preference<SavedUrl>(1);
  SavedUrl saved{};
  if (pref_.load(&saved) && std::memchr(saved.data, '\0', sizeof(saved.data)) != nullptr &&
      parent_->set_manifest_override_url(saved.data)) {
    this->publish_state(saved.data);
  } else {
    parent_->set_manifest_override_url("");
    this->publish_state("");
  }
}

void ManifestOverrideText::control(const std::string& value) {
  if (value.size() >= sizeof(SavedUrl::data) || !parent_->set_manifest_override_url(value)) {
    ESP_LOGW(TAG, "Invalid firmware manifest override: HTTPS URL required (max 255 chars)");
    return;
  }
  SavedUrl saved{};
  std::memcpy(saved.data, value.c_str(), value.size());
  if (!pref_.save(&saved)) ESP_LOGW(TAG, "Unable to persist manifest URL override");
  this->publish_state(value);
}

void RadioBridge::publish_firmware_update_status_() {
  const auto& status = flash_diagnostics_.status();
  if (reported_firmware_update_status_ == status) return;
  reported_firmware_update_status_ = status;
  if (firmware_update_status_sensor_ != nullptr) firmware_update_status_sensor_->publish_state(status);
}

void RadioBridge::flash_firmware() {
  if (flash_phase_ != FirmwareFlashPhase::IDLE) {
    ESP_LOGW(TAG, "Firmware update already in progress");
    return;  // Preserve the current combined progress/result sensor.
  }
  const bool configured = !error_ && uart_.ready() && driver_ != nullptr &&
                          driver_->profile().supports_bootloader && driver_->flash_protocol() != nullptr &&
                          http_request_ != nullptr && !catalogue_.busy();
  const firmware::FlashPreflight state{armed_,
                                       client_fd_ >= 0,
                                       identifying_,
                                       mode_ == Mode::NORMAL,
                                       network::is_connected(),
                                       configured,
                                       flash_phase_ != FirmwareFlashPhase::IDLE || downloader_.busy()};
  if (!firmware::can_flash(state)) {
    const char* reason = firmware::flash_block_reason(state);
    ESP_LOGW(TAG, "Firmware flash rejected: %s", reason != nullptr ? reason : "unknown");
    flash_diagnostics_.rejected(reason);
    this->publish_firmware_update_status_();
    return;
  }
  const firmware::FirmwareRelease* release = firmware::select_latest_release(catalogue_result_);
  if (release == nullptr) {
    flash_diagnostics_.failed("no valid firmware release selected");
    this->publish_firmware_update_status_();
    return;
  }
  if (!downloader_.start(http_request_, *release)) {
    flash_diagnostics_.failed("download could not start");
    this->publish_firmware_update_status_();
    return;
  }
  // Claim exclusive UART access before the worker starts; no ZHA client may connect.
  this->close_listener_();
  uart_to_tcp_.clear();
  tcp_to_uart_.clear();
  flash_source_epoch_ = catalogue_source_epoch_;
  flash_phase_ = FirmwareFlashPhase::DOWNLOADING;
  flash_diagnostics_.start();
  this->publish_firmware_update_status_();
  this->update_bridge_health_(network::is_connected());
}

void RadioBridge::poll_firmware_flash_() {
  if (flash_phase_ == FirmwareFlashPhase::DOWNLOADING) {
    const unsigned progress = downloader_.progress_percent();
    if (flash_diagnostics_.download_progress(progress)) this->publish_firmware_update_status_();
    firmware::DownloadedFirmware ready;
    if (!downloader_.collect(ready)) return;
    if (!ready.ok) {
      this->finish_firmware_flash_(false, ready.message);
      return;
    }
    if (!armed_ || client_fd_ >= 0 || !network::is_connected() ||
        flash_source_epoch_ != catalogue_source_epoch_) {
      this->finish_firmware_flash_(false, "maintenance, network or manifest source changed");
      return;
    }
    flash_image_ = std::move(ready);
    flash_diagnostics_.verified();
    this->publish_firmware_update_status_();
    if (!this->configure_uart_(Mode::BOOTLOADER)) {
      this->finish_firmware_flash_(false, "bootloader UART setup failed");
      return;
    }
    if (!driver_->enter_bootloader()) {
      this->finish_firmware_flash_(false, "bootloader entry failed");
      return;
    }
    auto* protocol = driver_->flash_protocol();
    if (protocol == nullptr || !protocol->begin(flash_image_.image.get(), millis())) {
      this->finish_firmware_flash_(false, "flash protocol could not start");
      return;
    }
    flash_phase_ = FirmwareFlashPhase::FLASHING;
    flash_diagnostics_.flashing();
    this->publish_firmware_update_status_();
    return;
  }
  if (flash_phase_ != FirmwareFlashPhase::FLASHING) return;
  auto* protocol = driver_->flash_protocol();
  if (protocol == nullptr) {
    this->finish_firmware_flash_(false, "flash protocol unavailable");
    return;
  }
  protocol->poll(uart_, millis());
  const unsigned progress = protocol->percent();
  if (flash_diagnostics_.flash_progress(progress)) this->publish_firmware_update_status_();
  if (protocol->succeeded()) {
    this->finish_firmware_flash_(true, "Completed");
  } else if (protocol->failed()) {
    this->finish_firmware_flash_(false, protocol->error());
  }
}

void RadioBridge::finish_firmware_flash_(bool success, const std::string& reason) {
  if (flash_phase_ == FirmwareFlashPhase::FLASHING && driver_->flash_protocol() != nullptr &&
      driver_->flash_protocol()->running())
    driver_->flash_protocol()->cancel();
  const bool in_bootloader = mode_ == Mode::BOOTLOADER;
  flash_phase_ = FirmwareFlashPhase::IDLE;
  flash_image_ = {};  // Release access to the verified flash-staging image.
  if (in_bootloader) {
    flash_diagnostics_.restoring();
    this->publish_firmware_update_status_();
    const bool reset_ok = driver_ != nullptr && driver_->exit_bootloader();
    const bool uart_ok = this->configure_uart_(Mode::NORMAL);
    if (!reset_ok || !uart_ok) {
      const char* reason = !reset_ok && !uart_ok ? "radio reset and UART restoration failed"
                           : !reset_ok           ? "radio reset failed"
                                                 : "UART restoration failed";
      flash_diagnostics_.failed(reason);
      this->publish_firmware_update_status_();
      armed_ = false;
      if (armed_switch_ != nullptr) armed_switch_->turn_off();
      this->report_error_("Radio recovery failed after firmware update");
      return;
    }
  }
  armed_ = false;
  if (armed_switch_ != nullptr) armed_switch_->turn_off();
  if (success)
    flash_diagnostics_.completed();
  else
    flash_diagnostics_.failed(reason);
  this->publish_firmware_update_status_();
  // New firmware may identify differently. Refresh the driver-owned installed version.
  if (in_bootloader) this->begin_radio_identification_();
  listener_retry_pending_ = false;
  this->update_bridge_health_(network::is_connected());
}

void RadioBridge::on_shutdown() {
  if (active_probe_ != nullptr && identifying_) active_probe_->cancel();
  if (flash_phase_ == FirmwareFlashPhase::FLASHING && driver_ != nullptr &&
      driver_->flash_protocol() != nullptr)
    driver_->flash_protocol()->cancel();
  identifying_ = false;
  this->publish_bridge_health_(BridgeHealth::STOPPED);
  this->close_client_(DisconnectReason::SHUTDOWN);
  this->close_listener_();
  // Release bootloader select and UART ownership during ESP32 shutdown.
  if (driver_ != nullptr) driver_->shutdown();
  uart_.stop();
}

}  // namespace esphome::esphome_radio_bridge
