"""Contract tests for the ESPHome daily firmware update monitoring branch."""

from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "esphome-radio-bridge"


class Loader(yaml.SafeLoader):
    pass


Loader.add_constructor("!secret", lambda loader, node: loader.construct_scalar(node))
Loader.add_constructor("!include", lambda loader, node: loader.construct_scalar(node))


def test_daily_update_checks_have_retry_and_no_uart_access():
    monitor = (COMPONENT / "firmware_update_monitor.h").read_text()
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    assert "24UL * 60UL * 60UL * 1000UL" in monitor
    assert "15UL * 60UL * 1000UL" in monitor
    assert "static_cast<uint32_t>(now_ms - last_attempt_ms)" in monitor
    assert "void RadioBridge::check_for_updates_if_due_()" in core
    assert "firmware::update_check_due(" in core
    assert "this->check_for_updates_if_due_();" in core
    assert "!network::is_connected() || catalogue_.busy()" in core
    assert "last_update_check_succeeded_ = catalogue_result_.ok" in core
    assert "has_update_check_attempt_ = false;  // Discard an obsolete in-flight request." in core
    assert "enter_bootloader()" not in monitor
    assert "uart_.read" not in monitor


def test_ambiguous_installed_version_uses_unknown_state():
    monitor = (COMPONENT / "firmware_update_monitor.h").read_text()
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    assert "Multiple releases share this radio version" in monitor
    assert "parse_radio_version(" in monitor
    assert "radio_responding" in monitor
    assert "update_available_sensor_->invalidate_state()" in core
    assert 'latest_firmware_sensor_->publish_state("Unknown")' in core
    assert "this->publish_update_assessment_()" in core


def test_new_entities_and_manual_check_are_configured():
    schema = (COMPONENT / "__init__.py").read_text()
    header = (COMPONENT / "esphome_radio_bridge.h").read_text()
    for key, setter in (
        ("latest_firmware", "set_latest_firmware_sensor"),
        ("update_available", "set_update_available_sensor"),
    ):
        assert f'"{key}"' in schema
        assert setter in schema
        assert setter in header
    assert 'CONF_CHECK_UPDATES = "check_updates"' in schema
    assert 'CheckUpdatesButton = ns.class_("CheckUpdatesButton", button.Button)' in schema
    assert "parent_->refresh_catalogue()" in header
    for path in (
        ROOT / "tests/components/esphome-radio-bridge/test.esp32-idf.yaml",
        ROOT / "examples/tubeszb-minimal.yaml",
        ROOT / "examples/tubeszb-mgm210-poe-2023.yaml",
    ):
        config = yaml.load(path.read_text(), Loader=Loader)["esphome-radio-bridge"]
        for key in ("check_updates", "latest_firmware", "update_available"):
            assert config[key]["name"]
        assert "catalogue_url" not in config
        assert config["manifest_override"]["name"]


def test_arduinojson_array_api_remains_compatible():
    source = (COMPONENT / "firmware_catalogue.cpp").read_text()
    assert "entries.size() != 0" in source
    assert "entries.empty()" not in source


def test_latest_firmware_entity_uses_radio_stack_version_not_release_id():
    header = (COMPONENT / "firmware_catalogue.h").read_text()
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    downloader = (COMPONENT / "firmware_catalogue.cpp").read_text()
    assert "latest_radio_version(const FirmwareLoadResult& metadata)" in header
    assert "release.firmware_version == metadata.latest" in header
    assert "return release.radio_version;" in header
    assert "firmware::latest_radio_version(catalogue_result_)" in core
    assert '"Loaded: " + latest_radio_version(out)' in downloader
    # Preserve release IDs for update comparison and download/flash selection.
    assert "firmware::assess_update(catalogue_result_" in core
    assert "firmware::select_latest_release(catalogue_result_)" in core
