"""Exercise code-generation requirements without importing ESPHome at host-test time."""

import ast
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/esphome-radio-bridge/__init__.py"


class InvalidConfiguration(Exception):
    def __init__(self, message, path=None):
        super().__init__(message)
        self.path = path


def _requirements(esp32_config=None, http_config=None):
    source = ast.parse(COMPONENT.read_text())
    functions = [
        node
        for node in source.body
        if isinstance(node, ast.FunctionDef)
        and node.name in ("_register_flash_partition", "_validate_firmware_http_request")
    ]
    registered = []
    env = {
        "CONF_FLASH_FIRMWARE": "flash_firmware",
        "CONF_DRIVER": "driver",
        "FLASH_STAGING": {"tubeszb_mgm210_poe_2023": ("radio_fw", "data", "undefined", 0x80000)},
        "MIN_FIRMWARE_HTTP_TX_BUFFER": 4096,
        "CORE": SimpleNamespace(config={"esp32": esp32_config or {}}),
        "esp32": SimpleNamespace(add_partition=lambda *args: registered.append(args)),
        "fv": SimpleNamespace(
            full_config=SimpleNamespace(get=lambda: {"http_request": http_config or {}})
        ),
        "cv": SimpleNamespace(Invalid=InvalidConfiguration),
    }
    exec(compile(ast.Module(body=functions, type_ignores=[]), str(COMPONENT), "exec"), env)
    return SimpleNamespace(
        register=env["_register_flash_partition"],
        validate=env["_validate_firmware_http_request"],
        registered=registered,
    )


def _flash_enabled():
    return {"driver": "tubeszb_mgm210_poe_2023", "flash_firmware": {}}


def test_flash_enabled_registers_partition_when_missing():
    subject = _requirements()
    subject.register(_flash_enabled())
    assert subject.registered == [("radio_fw", "data", "undefined", 0x80000)]


def test_monitor_only_does_not_allocate_staging():
    subject = _requirements()
    subject.register({"driver": "tubeszb_mgm210_poe_2023"})
    assert not subject.registered


def test_explicit_partition_preserved_without_duplicate():
    partitions = [{"name": "radio_fw", "type": "data", "subtype": "undefined", "size": 0x80000}]
    subject = _requirements(esp32_config={"partitions": partitions})
    subject.register(_flash_enabled())
    assert not subject.registered


def test_other_custom_partitions_remain_and_staging_is_added():
    subject = _requirements(esp32_config={"partitions": [{"name": "my_data"}]})
    subject.register(_flash_enabled())
    assert len(subject.registered) == 1


def test_custom_csv_requires_manually_provisioned_partition():
    subject = _requirements(esp32_config={"partitions": "custom.csv"})
    with pytest.raises(InvalidConfiguration, match="custom ESP32 partition CSV"):
        subject.register(_flash_enabled())


@pytest.mark.parametrize("tx_buffer", [0, 512, 1024, 2048])
def test_http_tx_buffer_rejected_when_too_small(tx_buffer):
    subject = _requirements(http_config={"buffer_size_tx": tx_buffer})
    with pytest.raises(InvalidConfiguration, match="buffer_size_tx: 4096"):
        subject.validate(_flash_enabled())


@pytest.mark.parametrize("tx_buffer", [4096, 8192])
def test_http_tx_buffer_accepts_sufficient_capacity(tx_buffer):
    subject = _requirements(http_config={"buffer_size_tx": tx_buffer})
    subject.validate(_flash_enabled())


@pytest.mark.parametrize(
    ("key", "value", "error"),
    [
        ("follow_redirects", False, "follow_redirects: true"),
        ("verify_ssl", False, "verify_ssl: true"),
    ],
)
def test_insecure_or_nonredirecting_http_rejected(key, value, error):
    subject = _requirements(http_config={"buffer_size_tx": 4096, key: value})
    with pytest.raises(InvalidConfiguration, match=error):
        subject.validate(_flash_enabled())


def test_monitor_only_skips_http_buffer_requirement():
    subject = _requirements()
    subject.validate({"driver": "tubeszb_mgm210_poe_2023"})
