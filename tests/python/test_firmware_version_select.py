from pathlib import Path

import yaml


class Loader(yaml.SafeLoader):
    pass


Loader.add_constructor("!include", lambda loader, node: loader.construct_scalar(node))
Loader.add_constructor("!secret", lambda loader, node: loader.construct_scalar(node))


ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/esphome-radio-bridge"


def test_select_is_exposed_and_registered():
    schema = (COMPONENT / "__init__.py").read_text()
    header = (COMPONENT / "esphome_radio_bridge.h").read_text()
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    for expected in (
        'CONF_FIRMWARE_VERSION_SELECT = "firmware_version_select"',
        'FirmwareVersionSelect = ns.class_("FirmwareVersionSelect", select.Select)',
        'select.register_select(entity, conf, options=["Latest"])',
        "var.set_firmware_version_select(entity)",
    ):
        assert expected in schema
    assert "class FirmwareVersionSelect : public select::Select" in header
    assert "set_firmware_version_choice" in header
    assert "firmware::select_firmware_release(catalogue_result_, selected_release_id_)" in core
    assert "refresh_firmware_choices_();" in core
    assert "selected_release_id_.clear();" in core
    assert "flash_phase_ != FirmwareFlashPhase::IDLE" in core


def test_full_examples_expose_firmware_version_selection():
    for name in (
        "examples/tubeszb-mgm210-poe-2023.yaml",
        "tests/components/esphome-radio-bridge/test.esp32-idf.yaml",
    ):
        config = yaml.load((ROOT / name).read_text(), Loader=Loader)
        assert config["esphome-radio-bridge"]["firmware_version_select"]["name"]
    minimal = yaml.load((ROOT / "examples/tubeszb-minimal.yaml").read_text(), Loader=Loader)
    assert "firmware_version_select" not in minimal["esphome-radio-bridge"]


def test_version_choices_are_cached_and_restored_before_api_discovery():
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    header = (COMPONENT / "esphome_radio_bridge.h").read_text()
    cache = (COMPONENT / "firmware_release_cache.h").read_text()
    schema = (COMPONENT / "__init__.py").read_text()
    setup = core.split("void RadioBridge::setup()", 1)[1].split("void RadioBridge::dump_config()", 1)[0]
    assert setup.index("restore_override()") < setup.index("restore_cache(")
    assert setup.index("restore_cache(") < setup.index("driver_->setup()")
    assert "make_entity_preference<firmware::FirmwareChoicesSnapshot>" in core
    assert "make_firmware_choices_snapshot" in core
    assert "read_firmware_choices_snapshot" in core
    assert "same_firmware_choices" in core
    assert "firmware_source_key" in cache
    assert "RELEASE_CACHE_MAX_CHOICES = 24" in cache
    assert "firmware_release_cache.h" in header
    assert 'CONF_FIRMWARE_VERSION_STATUS = "firmware_version_status"' in schema
    assert "set_firmware_version_status_sensor" in schema


def test_cached_versions_never_authorize_flashing_or_api_reset():
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    policy = (COMPONENT / "firmware_release_selection.h").read_text()
    assert "firmware::select_firmware_release(catalogue_result_, selected_release_id_)" in core
    assert "if (!metadata.ok || !valid_manifest_metadata(metadata)) return nullptr;" in policy
    assert "if (!catalogue_result_.ok || !firmware::valid_manifest_metadata(catalogue_result_))" in core
    assert "api::global_api_server" not in core
    assert "on_fatal_error()" not in core


def test_examples_expose_version_list_diagnostics():
    for name in (
        "examples/tubeszb-mgm210-poe-2023.yaml",
        "tests/components/esphome-radio-bridge/test.esp32-idf.yaml",
    ):
        config = yaml.load((ROOT / name).read_text(), Loader=Loader)
        assert config["esphome-radio-bridge"]["firmware_version_status"]["name"]
