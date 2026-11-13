from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/esphome-radio-bridge"


def test_modular_flash_contract_and_driver():
    interface = (COMPONENT / "flash_interface.h").read_text()
    board = (COMPONENT / "board_driver.h").read_text()
    driver = (COMPONENT / "tubeszb_mgm210_poe_2023.h").read_text()
    assert "class FlashProtocol" in interface
    assert "virtual firmware::FlashProtocol* flash_protocol()" in board
    assert "firmware::GeckoXmodem" in driver


def test_download_before_bootloader_and_exclusive_uart():
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    downloader = (COMPONENT / "firmware_downloader.cpp").read_text()
    assert core.index("downloader_.start(") < core.index(
        "driver_->enter_bootloader()", core.index("void RadioBridge::poll_firmware_flash_()")
    )
    assert "this->close_listener_();" in core
    assert "if (flash_phase_ != FirmwareFlashPhase::IDLE)" in core
    assert "SHA-256 checksum mismatch" in downloader
    assert "FlashPartitionStorage::open()" in downloader
    assert "response->end();" in downloader


def test_esphome_flash_entities_and_http_redirects():
    schema = (COMPONENT / "__init__.py").read_text()
    for name in ("CONF_FLASH_FIRMWARE", "CONF_FIRMWARE_UPDATE_STATUS"):
        assert name in schema
    # Flash-capable configs carry validated HTTP settings, but no explicit
    # partition declaration: the driver registers that at code generation.
    for yaml_file in (
        "examples/tubeszb-mgm210-poe-2023.yaml",
        "tests/components/esphome-radio-bridge/test.esp32-idf.yaml",
    ):
        body = (ROOT / yaml_file).read_text()
        assert "follow_redirects: true" in body
        assert "buffer_size_tx: 4096" in body
        assert "firmware_update_status:" in body
        assert "flash_firmware:" in body
        assert "flash_staging:" not in body

    minimal = (ROOT / "examples/tubeszb-minimal.yaml").read_text()
    assert "flash_firmware:" not in minimal
    assert "flash_armed:" not in minimal
    assert "check_updates:" in minimal
    assert "radio_fw" in (ROOT / "packages/radio-flash-staging.yaml").read_text()


def test_driver_registers_firmware_staging_and_validates_http_requirements():
    code = (COMPONENT / "__init__.py").read_text()
    assert "esp32.add_partition(*partition_spec)" in code
    assert '"tubeszb_mgm210_poe_2023": ("radio_fw", "data", "undefined", 0x80000)' in code
    assert "CONF_FLASH_FIRMWARE not in config" in code
    assert "MIN_FIRMWARE_HTTP_TX_BUFFER = 4096" in code
    assert "FINAL_VALIDATE_SCHEMA = _validate_firmware_http_request" in code
    assert 'request_config.get("follow_redirects", True)' in code
    assert 'request_config.get("verify_ssl", True)' in code
