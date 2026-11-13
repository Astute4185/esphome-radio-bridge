"""Static safeguards for the reversible UART spike."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/esphome-radio-bridge"


def test_runtime_uart_select_is_opt_in_and_has_a_wiring_guard():
    py = (COMPONENT / "__init__.py").read_text()
    assert 'CONF_UART_MODE_SELECT = "uart_mode_select"' in py
    assert 'CONF_FLOW_PINS_VERIFIED = "flow_control_wiring_verified"' in py
    assert "cv.Optional(CONF_FLOW_PINS_VERIFIED, default=False): cv.boolean" in py
    assert '"4-wire (profile RTS/CTS)"' in py
    assert '"4-wire (swapped RTS/CTS)"' in py
    assert "select.register_select(" in py


def test_runtime_switch_respects_board_bootloader_profile():
    code = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    assert "board.bootloader_uart_mode" in code
    assert "can_switch_normal_uart(" in code
    assert "flow_control_wiring_verified_" in code
    assert "this->begin_radio_identification_();" in code
    assert "this->publish_uart_mode_select_();" in code
    assert "this->configure_uart_(Mode::NORMAL)" in code


def test_two_wire_gpio_is_released_after_four_wire():
    code = (COMPONENT / "esp_idf_uart_bus.h").read_text()
    assert "gpio_reset_pin(" in code
    assert "UART_PIN_NO_CHANGE" in code
