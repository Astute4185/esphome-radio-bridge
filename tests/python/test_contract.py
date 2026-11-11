"""Static contracts run even if ESPHome is not installed."""

import re
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "serial_radio_gateway"


class Loader(yaml.SafeLoader):
    """Allow !secret placeholders for static YAML checks."""


Loader.add_constructor("!secret", lambda loader, node: loader.construct_scalar(node))
Loader.add_constructor("!include", lambda loader, node: loader.construct_scalar(node))


def test_external_component_layout():
    for name in (
        "__init__.py",
        "byte_ring.h",
        "mode_policy.h",
        "serial_radio_gateway.h",
        "serial_radio_gateway.cpp",
        "drivers/board_driver.h",
        "uart_modes/uart_mode.h",
        "uart_modes/two_wire.h",
        "uart_modes/four_wire.h",
        "uart_modes/mode_factory.h",
        "uart_modes/esp_idf_uart_bus.h",
        "drivers/board_factory.h",
        "drivers/tubeszb_mgm210_poe_2023.h",
        "drivers/tubeszb_mgm210_poe_2023_profile.h",
    ):
        assert (COMPONENT / name).exists()
    assert 'DOMAIN = "serial_radio_gateway"' in (COMPONENT / "__init__.py").read_text()


def test_board_specific_mapping_is_isolated():
    board = (COMPONENT / "drivers/tubeszb_mgm210_poe_2023_profile.h").read_text()
    assert re.search(r"\b1,\s*4,\s*36,\s*2,\s*13,\s*122", board)
    assert "UartModeId::FOUR_WIRE" in board
    assert "UartModeId::TWO_WIRE" in board
    assert re.search(r"\b5,\s*16,\s*// RESET", board)
    core = (COMPONENT / "serial_radio_gateway.cpp").read_text()
    assert "TUBESZB_MGM210" not in core
    assert "board::" not in core
    assert "uart_set_hw_flow_ctrl" not in core
    assert "uart_set_pin" not in core
    assert "uart_.configure" in core
    assert "uart_set_hw_flow_ctrl" in (COMPONENT / "uart_modes/esp_idf_uart_bus.h").read_text()
    schema = (COMPONENT / "__init__.py").read_text()
    assert 'CONF_DRIVER = "driver"' in schema
    assert '"hardware_flow_control"' not in schema
    assert '"rts_pin"' not in schema
    assert '"cts_pin"' not in schema


def test_yaml_examples_use_driver_and_generic_core():
    for path in (ROOT / "examples").glob("*.yaml"):
        if path.name == "secrets.example.yaml":
            continue
        config = yaml.load(path.read_text(), Loader=Loader)
        assert "serial_radio_gateway" in config
        assert config["serial_radio_gateway"]["driver"] == "tubeszb_mgm210_poe_2023"
        assert "uart" not in config
        assert "stream_server" not in config
        assert "efr32_flasher" not in config
        assert "board_network" in config["packages"]
        assert "esp32-wrover-lan8720.yaml" in config["packages"]["board_network"]
        assert config["serial_radio_gateway"]["tcp_port"] == 6638


def test_esphome_build_fixture():
    path = ROOT / "tests/components/serial_radio_gateway/test.esp32-idf.yaml"
    doc = yaml.load(path.read_text(), Loader=Loader)
    assert doc["serial_radio_gateway"]["driver"] == "tubeszb_mgm210_poe_2023"
    assert "board_driver" not in doc


def test_no_unverified_production_driver():
    schema = (COMPONENT / "__init__.py").read_text()
    assert 'BOARD_DRIVERS = ("tubeszb_mgm210_poe_2023",)' in schema
    assert (ROOT / "docs/adding_drivers.md").exists()


def test_shared_baseboard_package_is_radio_agnostic():
    pkg = yaml.load((ROOT / "packages/esp32-wrover-lan8720.yaml").read_text(), Loader=Loader)
    assert pkg["esp32"]["board"] == "esp-wrover-kit"
    assert pkg["esp32"]["framework"]["type"] == "esp-idf"
    assert pkg["ethernet"]["type"] == "LAN8720"
    assert "serial_radio_gateway" not in pkg
    assert "tubeszb_mgm210" not in repr(pkg)


def test_driver_owns_uart_mode_selection():
    driver = (COMPONENT / "drivers/board_driver.h").read_text()
    core = (COMPONENT / "serial_radio_gateway.cpp").read_text()
    schema = (COMPONENT / "__init__.py").read_text()
    assert "normal_uart_mode" in driver
    assert "bootloader_uart_mode" in driver
    assert "board.normal_uart_mode" in core
    assert "board.bootloader_uart_mode" in core
    assert '"uart_mode"' not in schema
