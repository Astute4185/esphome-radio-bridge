"""Static contracts run even if ESPHome is not installed."""

import re
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "esphome-radio-bridge"


class Loader(yaml.SafeLoader):
    """Allow !secret placeholders for static YAML checks."""


Loader.add_constructor("!secret", lambda loader, node: loader.construct_scalar(node))
Loader.add_constructor("!include", lambda loader, node: loader.construct_scalar(node))


def test_external_component_layout():
    for name in (
        "__init__.py",
        "byte_ring.h",
        "mode_policy.h",
        "esphome_radio_bridge.h",
        "esphome_radio_bridge.cpp",
        "board_driver.h",
        "radio_identification.h",
        "efr32_ezsp_probe.h",
        "uart_mode.h",
        "two_wire.h",
        "four_wire.h",
        "mode_factory.h",
        "esp_idf_uart_bus.h",
        "board_factory.h",
        "tubeszb_mgm210_poe_2023.h",
        "tubeszb_mgm210_poe_2023_profile.h",
    ):
        assert (COMPONENT / name).exists()
    schema = (COMPONENT / "__init__.py").read_text()
    assert f'DOMAIN = "{COMPONENT.name}"' in schema
    # C++ identifiers cannot contain hyphens, so the C++ namespace uses underscores.
    assert 'ns = cg.esphome_ns.namespace("esphome_radio_bridge")' in schema
    assert (
        "namespace esphome::esphome_radio_bridge" in (COMPONENT / "esphome_radio_bridge.h").read_text()
    )


def test_board_specific_mapping_is_isolated():
    board = (COMPONENT / "tubeszb_mgm210_poe_2023_profile.h").read_text()
    assert re.search(r"\b1,\s*4,\s*36,\s*2,\s*13,\s*122", board)
    # POC uses the existing two-wire NCP image; physical RTS/CTS pin mapping is retained.
    assert re.search(r"UartModeId::TWO_WIRE,\s*// normal mode", board)
    assert re.search(r"UartModeId::TWO_WIRE,\s*// bootloader mode", board)
    assert re.search(r"\b5,\s*16,\s*// RESET", board)
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    assert "TUBESZB_MGM210" not in core
    assert "board::" not in core
    assert "uart_set_hw_flow_ctrl" not in core
    assert "uart_set_pin" not in core
    assert "uart_.configure" in core
    assert "uart_set_hw_flow_ctrl" in (COMPONENT / "esp_idf_uart_bus.h").read_text()
    schema = (COMPONENT / "__init__.py").read_text()
    assert 'CONF_DRIVER = "driver"' in schema
    assert '"hardware_flow_control"' not in schema
    assert '"rts_pin"' not in schema
    assert '"cts_pin"' not in schema


def test_yaml_examples_use_driver_and_generic_core():
    for path in (ROOT / "examples").glob("tubeszb-*.yaml"):
        config = yaml.load(path.read_text(), Loader=Loader)
        assert COMPONENT.name in config
        assert config["external_components"][0]["components"] == [COMPONENT.name]
        assert config["esphome-radio-bridge"]["driver"] == "tubeszb_mgm210_poe_2023"
        assert "uart" not in config
        assert "stream_server" not in config
        assert "efr32_flasher" not in config
        assert "board_network" in config["packages"]
        assert "esp32-wrover-lan8720.yaml" in config["packages"]["board_network"]
        assert config["esphome-radio-bridge"]["tcp_port"] == 6638


def test_esphome_build_fixture():
    path = ROOT / "tests/components/esphome-radio-bridge/test.esp32-idf.yaml"
    doc = yaml.load(path.read_text(), Loader=Loader)
    assert doc[COMPONENT.name]["driver"] == "tubeszb_mgm210_poe_2023"
    assert doc["external_components"][0]["components"] == [COMPONENT.name]
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
    assert "esphome-radio-bridge" not in pkg
    assert "tubeszb_mgm210" not in repr(pkg)


def test_driver_owns_uart_mode_selection():
    driver = (COMPONENT / "board_driver.h").read_text()
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    schema = (COMPONENT / "__init__.py").read_text()
    assert "normal_uart_mode" in driver
    assert "bootloader_uart_mode" in driver
    assert "board.normal_uart_mode" in core
    assert "board.bootloader_uart_mode" in core
    assert '"uart_mode"' not in schema


def test_component_cpp_files_are_placed_at_root():
    # ESPHome copies C++ sources/headers from the component directory itself.
    assert not (COMPONENT / "drivers").exists()
    assert not (COMPONENT / "uart_modes").exists()
    for source in COMPONENT.glob("*.[ch]*"):
        content = source.read_text()
        assert '#include "drivers/' not in content
        assert '#include "uart_modes/' not in content


def test_ci_runs_real_compile_gate():
    compile_script = (ROOT / "scripts/compile.sh").read_text()
    workflow = (ROOT / ".github/workflows/ci.yaml").read_text()
    assert "esphome compile" in compile_script
    assert "./scripts/compile.sh" in workflow


def test_bridge_health_entities_and_wiring():
    schema = (COMPONENT / "__init__.py").read_text()
    header = (COMPONENT / "esphome_radio_bridge.h").read_text()
    source = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    health = (COMPONENT / "bridge_health.h").read_text()
    for key, setter in (
        ("bridge_ready", "set_bridge_ready_sensor"),
        ("bridge_status", "set_bridge_status_sensor"),
        ("last_disconnect_reason", "set_last_disconnect_reason_sensor"),
        ("transport_errors", "set_transport_errors_sensor"),
    ):
        assert f'"{key}"' in schema
        assert setter in schema
        assert setter in header
    assert "evaluate_bridge_health(" in source
    assert "listener_retry_due(" in source
    assert "publish_initial_state(" in source
    assert "NETWORK_OFFLINE" in health
    assert "MAINTENANCE" in health
    assert "REMOTE_CLOSED" in health
    assert "NETWORK_LOST" in health
    assert "is_transport_error(" in health


def test_bridge_health_test_yaml_defines_diagnostics():
    fixture = yaml.load(
        (ROOT / "tests/components/esphome-radio-bridge/test.esp32-idf.yaml").read_text(),
        Loader=Loader,
    )
    full_example = yaml.load((ROOT / "examples/tubeszb-mgm210-poe-2023.yaml").read_text(), Loader=Loader)
    for doc in (fixture, full_example):
        config = doc[COMPONENT.name]
        for field in ("bridge_ready", "bridge_status", "last_disconnect_reason", "transport_errors"):
            assert config[field]["name"]
        assert "connected" in config
        assert "disconnects" in config


def test_driver_owned_radio_probe_on_uart_startup():
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    contract = (COMPONENT / "board_driver.h").read_text()
    driver = (COMPONENT / "tubeszb_mgm210_poe_2023.h").read_text()
    probe = (COMPONENT / "efr32_ezsp_probe.h").read_text()
    assert "identification_probe()" in contract
    assert "identification_probe() override" in driver
    assert "begin_radio_identification_();" in core
    assert "poll_radio_identification_();" in core
    assert "close_listener_();" in core
    assert "No ASH RSTACK response" in probe
    assert "GET_VALUE" in probe
    assert "return;  // Exclusive UART ownership" in core


def test_radio_probe_entities_and_yaml():
    schema = (COMPONENT / "__init__.py").read_text()
    header = (COMPONENT / "esphome_radio_bridge.h").read_text()
    for key, setter in (
        ("installed_firmware", "set_installed_firmware_sensor"),
        ("radio_identification_status", "set_radio_identification_status_sensor"),
    ):
        assert f'"{key}"' in schema
        assert setter in schema
        assert setter in header
    assert 'CONF_PROBE_RADIO = "probe_radio"' in schema
    for path in [
        ROOT / "tests/components/esphome-radio-bridge/test.esp32-idf.yaml",
        ROOT / "examples/tubeszb-minimal.yaml",
        ROOT / "examples/tubeszb-mgm210-poe-2023.yaml",
    ]:
        config = yaml.load(path.read_text(), Loader=Loader)[COMPONENT.name]
        assert config["installed_firmware"]["name"]
        assert config["radio_identification_status"]["name"]
        assert config["probe_radio"]["name"]
