"""Contract tests for the optional normal UART override and reporting text."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMP = ROOT / "components/esphome-radio-bridge"


def test_schema_exposes_only_three_uart_choices():
    source = (COMP / "__init__.py").read_text()
    assert 'CONF_UART_MODE = "uart_mode"' in source
    assert 'cv.Optional(CONF_UART_MODE, default="driver")' in source
    assert '"driver", "2_wire", "4_wire", lower=True' in source
    assert "set_normal_uart_mode_override(" in source
    assert '"default_entities"' not in source
    assert "ENTITY_DEFAULT_NAMES" not in source


def test_override_applies_to_normal_and_bootloader_remains_driver_owned():
    core = (COMP / "esphome_radio_bridge.cpp").read_text()
    header = (COMP / "esphome_radio_bridge.h").read_text()
    assert "normal_uart_mode_ = uart_modes::effective_normal_uart_mode(" in core
    assert "valid_uart_pinout(board.uart, normal_uart_mode_, board.bootloader_uart_mode)" in core
    assert (
        "uart_.start(board.uart, board.normal_baud, normal_uart_mode_, board.bootloader_uart_mode)"
        in core
    )
    assert "new_mode == Mode::NORMAL ? normal_uart_mode_ : board.bootloader_uart_mode" in core
    assert "set_normal_uart_mode_override" in header


def test_mode_text_reports_effective_uart_and_bootloader():
    core = (COMP / "esphome_radio_bridge.cpp").read_text()
    helpers = (COMP / "normal_uart_mode.h").read_text()
    assert "mode_ == Mode::NORMAL ? normal_uart_mode_ : board.bootloader_uart_mode" in core
    assert "operating_mode_name(wire_mode, mode_ == Mode::BOOTLOADER)" in core
    for label in ('"4-wire"', '"2-wire"', '"2-wire bootloader"', '"4-wire bootloader"'):
        assert label in helpers


def test_example_contains_optional_driver_setting_without_entity_autocreation():
    full = (ROOT / "examples/tubeszb-mgm210-poe-2023.yaml").read_text()
    assert "  uart_mode: driver" in full
    assert "default_entities:" not in full
    assert "mode:\n    name: EFR32 Operating Mode" in full
