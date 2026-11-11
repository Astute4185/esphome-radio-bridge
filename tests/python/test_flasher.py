"""Host-side tests use simulated Gecko responses and never touch hardware."""

from __future__ import annotations

import hashlib
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import flash_gecko as flash  # noqa: E402


class FakeGecko:
    def __init__(self, *, retry_first: bool = False, cancel: bool = False):
        self.responses = list(b"Gecko Bootloader v2\r\nBL >")
        self.frames = []
        self.retry_first = retry_first
        self.cancel = cancel
        self.closed = False
        self.timeout = 0

    def settimeout(self, value: float) -> None:
        self.timeout = value

    def recv(self, size: int) -> bytes:
        if self.closed or not self.responses:
            return b""
        return bytes([self.responses.pop(0)])

    def sendall(self, chunk: bytes) -> None:
        if chunk == b"1":
            self.responses.append(ord("C"))
        elif chunk == bytes((flash.EOT,)):
            self.responses.extend(bytes([flash.ACK]) + b"Serial upload complete\r\nBL >")
        elif len(chunk) == 133 and chunk[0] == flash.SOH:
            self.frames.append(chunk)
            if self.cancel:
                self.responses.append(flash.CAN)
            elif self.retry_first and len(self.frames) == 1:
                self.responses.append(flash.NAK)
            else:
                self.responses.append(flash.ACK)
        else:
            raise AssertionError(f"Unexpected TX frame: {chunk!r}")


def test_crc_known_xmodem_vector():
    assert flash.crc16_xmodem(b"123456789") == 0x31C3


def test_frame_padding_number_rollover_and_crc():
    frame = flash.encode_packet(256, b"ABC")
    assert len(frame) == 133
    assert frame[:3] == bytes([flash.SOH, 0, 255])
    assert frame[3:6] == b"ABC"
    assert frame[6:131] == b"\x1a" * 125
    assert frame[-2:] == flash.crc16_xmodem(frame[3:131]).to_bytes(2, "big")
    with pytest.raises(ValueError):
        flash.encode_packet(0, b"A" * 129)


def test_simulated_bootloader_transfer():
    firmware = bytes(range(255))
    device = FakeGecko()
    flash.transfer_xmodem(device, firmware)
    assert len(device.frames) == 2
    assert device.frames[0][1] == 1
    assert device.frames[1][1] == 2


def test_retransmit_nak():
    device = FakeGecko(retry_first=True)
    flash.transfer_xmodem(device, b"A")
    assert len(device.frames) == 2
    assert device.frames[0] == device.frames[1]


def test_device_cancel_aborts():
    with pytest.raises(flash.FlashError, match="cancelled"):
        flash.transfer_xmodem(FakeGecko(cancel=True), b"A")


def test_manifest_validation_and_limits():
    import json

    image = b"hello"
    document = {
        "fw_url": "https://example.com/file.gbl",
        "md5": hashlib.md5(image, usedforsecurity=False).hexdigest(),
        "size": len(image),
        "baudrate": 115200,
        "version": "9.1",
    }
    assert flash.validate_manifest(json.dumps(document).encode()) == document
    for key, invalid in (("size", -1), ("baudrate", 921600), ("fw_url", "http://evil/file.gbl")):
        changed = dict(document)
        changed[key] = invalid
        with pytest.raises(flash.FlashError):
            flash.validate_manifest(json.dumps(changed).encode())


def test_get_firmware_file(tmp_path):
    binary = tmp_path / "radio.gbl"
    binary.write_bytes(b"abc")
    assert flash.get_firmware(filename=binary, manifest_url=None) == b"abc"
    with pytest.raises(flash.FlashError):
        flash.get_firmware(filename=None, manifest_url=None)


def test_no_flash_without_hardware_flow_confirmation(tmp_path):
    binary = tmp_path / "radio.gbl"
    binary.write_bytes(b"abc")
    with pytest.raises(flash.FlashError, match="confirm-hw-flow"):
        flash.main(["--file", str(binary), "--host", "192.0.2.1", "--manual"])
