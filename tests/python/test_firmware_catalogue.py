"""Firmware metadata schema regression tests independent of ESPHome runtime."""

import json
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "esphome-radio-bridge"


class Loader(yaml.SafeLoader):
    pass


Loader.add_constructor("!secret", lambda loader, node: loader.construct_scalar(node))
Loader.add_constructor("!include", lambda loader, node: loader.construct_scalar(node))


def test_default_catalogue_resolves_to_existing_manifest():
    catalogue = json.loads((ROOT / "firmware/catalogue.json").read_text())
    assert catalogue["schema_version"] == 1
    manifest_path = catalogue["boards"]["tubeszb_mgm210_poe_2023"]["zha"]
    manifest = json.loads((ROOT / manifest_path).read_text())
    assert manifest["schema_version"] == 1
    assert manifest["board"] == "tubeszb_mgm210_poe_2023"
    assert manifest["radio_type"] == "zha"
    assert manifest["latest"] in {version["firmware_version"] for version in manifest["versions"]}
    for version in manifest["versions"]:
        assert version["release_type"] in {"beta", "candidate", "release"}
        assert len(version["sha256"]) == 64
        assert version["file"].startswith("https://github.com/")
        assert "/releases/download/" in version["file"]


def test_catalogue_resolver_is_driver_agnostic():
    header = (COMPONENT / "firmware_catalogue.h").read_text()
    source = (COMPONENT / "firmware_catalogue.cpp").read_text()
    driver = (COMPONENT / "board_driver.h").read_text()
    assert "const char* radio_type" in driver
    assert "tubeszb_mgm210_poe_2023" not in (header + source)
    assert 'root["boards"]' in source
    assert 'root["versions"]' in source
    assert 'root["schema_version"]' in source
    assert 'root["latest"]' in source
    assert 'root["sha256"]' not in source  # Each entry is verified by its field name.
    assert "valid_manifest_metadata(out)" in source
    assert "valid_sha256(release.sha256)" in header
    assert "MAX_JSON_BYTES = 16384" in source
    assert "FirmwareCatalogue::worker_" in source


def test_catalogue_configuration_and_entities():
    schema = (COMPONENT / "__init__.py").read_text()
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    for name in ("catalogue_url", "manifest_override", "catalogue_status", "refresh_catalogue"):
        assert f'"{name}"' in schema
    assert "set_catalogue_url" in schema
    assert "set_manifest_override_text" in schema
    assert "set_catalogue_status_sensor" in schema
    assert "restore_override()" in core
    assert "make_entity_preference" in core
    assert "catalogue_.collect" in core
    for yaml_file in (
        ROOT / "tests/components/esphome-radio-bridge/test.esp32-idf.yaml",
        ROOT / "examples/tubeszb-minimal.yaml",
        ROOT / "examples/tubeszb-mgm210-poe-2023.yaml",
    ):
        config = yaml.load(yaml_file.read_text(), Loader=Loader)
        assert config["http_request"]["verify_ssl"] is True
        bridge = config["esphome-radio-bridge"]
        assert "catalogue_url" not in bridge  # Driver provides the canonical URL.
        assert bridge["manifest_override"]["name"]
        assert bridge["catalogue_status"]["name"]
        assert bridge["refresh_catalogue"]["name"]


def test_driver_default_catalogue_and_explicit_yaml_override_contract():
    schema = (COMPONENT / "__init__.py").read_text()
    contract = (COMPONENT / "board_driver.h").read_text()
    driver = (COMPONENT / "tubeszb_mgm210_poe_2023.h").read_text()
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    assert "cv.Optional(CONF_CATALOGUE_URL): secure_url" in schema
    assert "default_catalogue_url()" in contract
    assert "default_catalogue_url() const override" in driver
    assert (
        "https://raw.githubusercontent.com/Astute4185/esphome-radio-bridge/main/firmware/catalogue.json"
        in driver
    )
    assert "firmware::select_catalogue_url(catalogue_url_, driver_default)" in core
    assert "driver_->default_catalogue_url()" in core
    assert "catalogue_.start(http_request_, catalogue_url," in core
    assert "effective_catalogue_url_()" in core
    assert "Board driver default" in core
    assert "YAML override" in core
