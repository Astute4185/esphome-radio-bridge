from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/esphome-radio-bridge"


def test_unified_status_entity_and_diagnostic_model():
    schema = (COMPONENT / "__init__.py").read_text()
    core = (COMPONENT / "esphome_radio_bridge.cpp").read_text()
    header = (COMPONENT / "esphome_radio_bridge.h").read_text()
    assert "CONF_FIRMWARE_UPDATE_STATUS" in schema
    assert "FirmwareFlashDiagnostics flash_diagnostics_" in header
    assert "flash_diagnostics_.start()" in core
    assert "flash_diagnostics_.download_progress(progress)" in core
    assert "flash_diagnostics_.flash_progress(progress)" in core
    assert "flash_diagnostics_.restoring()" in core
    assert "flash_diagnostics_.completed()" in core
    assert "flash_diagnostics_.failed(reason)" in core
    assert "progress_sensor" not in schema


def test_failure_reason_and_rejection_policies():
    policy = (COMPONENT / "firmware_flash_policy.h").read_text()
    model = (COMPONENT / "firmware_flash_diagnostics.h").read_text()
    assert "flash_block_reason(const FlashPreflight& state)" in policy
    assert "arm firmware maintenance first" in policy
    assert "disconnect the TCP client first" in policy
    assert "radio identification is active" in policy
    assert "network offline" in policy
    assert "stage_ == FlashDiagnosticStage::COMPLETED" in model
    assert "stage_ == FlashDiagnosticStage::FAILED" in model
    assert "safe_reason_" in model
