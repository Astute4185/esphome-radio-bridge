#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "board_factory.h"
#include "bridge_health.h"
#include "byte_ring.h"
#include "esp_idf_uart_bus.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "firmware_catalogue.h"
#include "firmware_downloader.h"
#include "firmware_flash_diagnostics.h"
#include "firmware_flash_policy.h"
#include "firmware_release_cache.h"
#include "firmware_release_selection.h"
#include "firmware_update_monitor.h"
#include "mode_policy.h"
#include "normal_uart_mode.h"

namespace esphome::esphome_radio_bridge {

class ManifestOverrideText;
class FirmwareVersionSelect;

class RadioBridge : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_board_model(drivers::BoardModel model) { driver_ = drivers::create_board_driver(model); }
  void set_normal_uart_mode_override(uart_modes::NormalUartModeOverride selection) {
    normal_uart_override_ = selection;
  }
  void set_tcp_port(uint16_t port) { tcp_port_ = port; }
  void set_http_request(http_request::HttpRequestComponent* request) { http_request_ = request; }
  void set_catalogue_url(const std::string& url) { catalogue_url_ = url; }
  void set_manifest_override_text(ManifestOverrideText* value) { manifest_override_text_ = value; }
  void set_catalogue_status_sensor(text_sensor::TextSensor* value) { catalogue_status_sensor_ = value; }
  void set_latest_firmware_sensor(text_sensor::TextSensor* value) { latest_firmware_sensor_ = value; }
  void set_firmware_version_select(FirmwareVersionSelect* value) { firmware_version_select_ = value; }
  bool set_firmware_version_choice(const std::string& label);
  void set_firmware_version_status_sensor(text_sensor::TextSensor* value) {
    firmware_version_status_sensor_ = value;
  }
  void set_firmware_update_status_sensor(text_sensor::TextSensor* value) {
    firmware_update_status_sensor_ = value;
  }
  void set_update_available_sensor(binary_sensor::BinarySensor* value) { update_available_sensor_ = value; }
  bool set_manifest_override_url(const std::string& url);
  void refresh_catalogue();
  void set_maintenance_timeout(uint32_t timeout_ms) { maintenance_timeout_ms_ = timeout_ms; }
  void set_connected_sensor(binary_sensor::BinarySensor* value) { connected_sensor_ = value; }
  void set_bridge_ready_sensor(binary_sensor::BinarySensor* value) { bridge_ready_sensor_ = value; }
  void set_bridge_status_sensor(text_sensor::TextSensor* value) { bridge_status_sensor_ = value; }
  void set_last_disconnect_reason_sensor(text_sensor::TextSensor* value) {
    last_disconnect_reason_sensor_ = value;
  }
  void set_transport_errors_sensor(sensor::Sensor* value) { transport_errors_sensor_ = value; }
  void set_disconnects_sensor(sensor::Sensor* value) { disconnects_sensor_ = value; }
  void set_mode_sensor(text_sensor::TextSensor* value) { mode_sensor_ = value; }
  void set_installed_firmware_sensor(text_sensor::TextSensor* value) { installed_firmware_sensor_ = value; }
  void set_radio_identification_status_sensor(text_sensor::TextSensor* value) {
    radio_identification_status_sensor_ = value;
  }
  void set_armed_switch(switch_::Switch* value) { armed_switch_ = value; }

  void set_armed(bool armed) { armed_ = armed; }
  bool is_armed() const { return armed_; }
  void radio_reset();
  void probe_radio();
  void flash_firmware();
  void enter_bootloader();
  void exit_bootloader();

 protected:
  bool configure_uart_(Mode new_mode);
  void open_listener_();
  void close_listener_();
  void close_client_(DisconnectReason reason);
  void accept_client_();
  void bridge_uart_();
  void bridge_tcp_();
  void send_tcp_();
  void publish_mode_();
  void mark_maintenance_activity_();
  void report_error_(const char* message);
  void publish_bridge_health_(BridgeHealth health);
  void update_bridge_health_(bool network_ready);
  void record_transport_error_();
  void begin_radio_identification_();
  void poll_radio_identification_();
  void publish_radio_identity_(const identification::RadioIdentity& identity);
  void publish_catalogue_status_(const std::string& status);
  void check_for_updates_if_due_();
  void publish_update_assessment_();
  void clear_update_entities_();
  void refresh_firmware_choices_();
  void publish_firmware_version_status_(const std::string& status);
  uint64_t firmware_choices_source_key_() const;
  void publish_firmware_update_status_();
  void poll_firmware_flash_();
  void finish_firmware_flash_(bool success, const std::string& reason);

  std::unique_ptr<drivers::BoardDriver> driver_;
  firmware::FirmwareCatalogue catalogue_{};
  firmware::FirmwareDownloader downloader_{};
  enum class FirmwareFlashPhase : uint8_t { IDLE, DOWNLOADING, FLASHING };
  FirmwareFlashPhase flash_phase_{FirmwareFlashPhase::IDLE};
  firmware::DownloadedFirmware flash_image_{};
  text_sensor::TextSensor* firmware_update_status_sensor_{nullptr};
  std::string reported_firmware_update_status_{};
  firmware::FirmwareFlashDiagnostics flash_diagnostics_{};
  uint32_t flash_source_epoch_{0};
  firmware::FirmwareLoadResult catalogue_result_{};
  FirmwareVersionSelect* firmware_version_select_{nullptr};
  text_sensor::TextSensor* firmware_version_status_sensor_{nullptr};
  std::string firmware_version_status_{};
  bool firmware_ha_reload_pending_{false};
  bool component_setup_complete_{false};
  std::string selected_release_id_{};
  http_request::HttpRequestComponent* http_request_{nullptr};
  ManifestOverrideText* manifest_override_text_{nullptr};
  text_sensor::TextSensor* catalogue_status_sensor_{nullptr};
  text_sensor::TextSensor* latest_firmware_sensor_{nullptr};
  binary_sensor::BinarySensor* update_available_sensor_{nullptr};
  std::string effective_catalogue_url_() const;
  std::string catalogue_url_{};  // Explicit YAML override; empty selects the driver default.
  std::string manifest_override_url_{};
  std::string reported_catalogue_status_{};
  uint32_t catalogue_source_epoch_{0};
  uint32_t catalogue_request_epoch_{0};
  uint32_t last_update_check_ms_{0};
  bool has_update_check_attempt_{false};
  bool last_update_check_succeeded_{false};
  bool installed_radio_responding_{false};
  int listener_fd_{-1};
  int client_fd_{-1};
  uint16_t tcp_port_{6638};
  uint32_t maintenance_timeout_ms_{600000};
  uint32_t last_maintenance_activity_{0};
  uint32_t disconnects_{0};
  uint32_t transport_errors_{0};
  uint32_t last_listener_attempt_ms_{0};
  static constexpr uint32_t LISTENER_RETRY_MS = 5000;
  bool listener_retry_pending_{false};
  bool health_published_{false};
  BridgeHealth published_health_{BridgeHealth::STARTING};
  const char* last_failure_{nullptr};
  Mode mode_{Mode::NORMAL};
  uart_modes::NormalUartModeOverride normal_uart_override_{uart_modes::NormalUartModeOverride::DRIVER};
  uart_modes::UartModeId normal_uart_mode_{uart_modes::UartModeId::TWO_WIRE};
  bool armed_{false};
  bool identifying_{false};
  identification::RadioProbe* active_probe_{nullptr};
  std::string reported_firmware_{};
  std::string reported_identification_status_{};
  uart_modes::EspIdfUartBus uart_{};
  bool error_{false};
  ByteRing<4096> uart_to_tcp_{};
  ByteRing<2048> tcp_to_uart_{};
  binary_sensor::BinarySensor* connected_sensor_{nullptr};
  binary_sensor::BinarySensor* bridge_ready_sensor_{nullptr};
  text_sensor::TextSensor* bridge_status_sensor_{nullptr};
  text_sensor::TextSensor* last_disconnect_reason_sensor_{nullptr};
  sensor::Sensor* transport_errors_sensor_{nullptr};
  sensor::Sensor* disconnects_sensor_{nullptr};
  text_sensor::TextSensor* mode_sensor_{nullptr};
  text_sensor::TextSensor* installed_firmware_sensor_{nullptr};
  text_sensor::TextSensor* radio_identification_status_sensor_{nullptr};
  switch_::Switch* armed_switch_{nullptr};
};

// Options are rebuilt on every manifest load, including a custom override.
// Options must outlive SelectTraits' borrowed string pointers.
class FirmwareVersionSelect : public select::Select {
 public:
  explicit FirmwareVersionSelect(RadioBridge* parent) : parent_(parent) {}
  void update_choices(const firmware::FirmwareLoadResult& metadata, const std::string& selected_release_id,
                      uint64_t source_key);
  std::string restore_cache(uint64_t source_key);
  bool find_release_id(const std::string& label, std::string& release_id) const;
  bool choices_changed(const firmware::FirmwareLoadResult& metadata) const;
  size_t release_count() const { return choices_.empty() ? 0 : choices_.size() - 1; }
  void save_selection(uint64_t source_key, const std::string& selected_release_id);

 protected:
  void control(const std::string& value) override;
  RadioBridge* parent_;
  std::vector<firmware::FirmwareChoice> choices_{};
  ESPPreferenceObject pref_{};
  uint64_t source_key_{0};
  bool have_snapshot_{false};
  bool pref_ready_{false};
  std::string last_saved_selected_id_{};
  std::vector<firmware::FirmwareChoice> last_saved_choices_{};
  void set_choices_(std::vector<firmware::FirmwareChoice> choices, const std::string& selected_id);
};

class FlashFirmwareButton : public button::Button {
 public:
  explicit FlashFirmwareButton(RadioBridge* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->flash_firmware(); }
  RadioBridge* parent_;
};

class CheckUpdatesButton : public button::Button {
 public:
  explicit CheckUpdatesButton(RadioBridge* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->refresh_catalogue(); }
  RadioBridge* parent_;
};

class RefreshCatalogueButton : public button::Button {
 public:
  explicit RefreshCatalogueButton(RadioBridge* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->refresh_catalogue(); }
  RadioBridge* parent_;
};

// An editable manifest URL override with NVS-backed persistence.
// Clearing the text input re-enables the default catalogue lookup.
class ManifestOverrideText : public text::Text {
 public:
  explicit ManifestOverrideText(RadioBridge* parent) : parent_(parent) {}
  void restore_override();

 protected:
  void control(const std::string& value) override;
  struct SavedUrl {
    char data[256]{};
  };
  RadioBridge* parent_;
  ESPPreferenceObject pref_{};
};

class ProbeRadioButton : public button::Button {
 public:
  explicit ProbeRadioButton(RadioBridge* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->probe_radio(); }
  RadioBridge* parent_;
};

class RadioResetButton : public button::Button {
 public:
  explicit RadioResetButton(RadioBridge* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->radio_reset(); }
  RadioBridge* parent_;
};

class EnterBootloaderButton : public button::Button {
 public:
  explicit EnterBootloaderButton(RadioBridge* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->enter_bootloader(); }
  RadioBridge* parent_;
};

class ExitBootloaderButton : public button::Button {
 public:
  explicit ExitBootloaderButton(RadioBridge* parent) : parent_(parent) {}

 protected:
  void press_action() override { parent_->exit_bootloader(); }
  RadioBridge* parent_;
};

class FlashArmedSwitch : public switch_::Switch {
 public:
  explicit FlashArmedSwitch(RadioBridge* parent) : parent_(parent) {}

 protected:
  void write_state(bool state) override {
    parent_->set_armed(state);
    this->publish_state(state);
  }
  RadioBridge* parent_;
};

}  // namespace esphome::esphome_radio_bridge
