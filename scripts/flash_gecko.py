#!/usr/bin/env python3
"""Validate a GBL locally, then transfer it using Gecko UART XMODEM-CRC.

The ESPHome board component owns UART/GPIO/TCP and the mode transitions.
Home Assistant's encrypted API is used to arm and switch modes, unless --manual.
No custom unauthenticated firmware-management API is exposed by the ESP32.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import socket
import sys
import time
from pathlib import Path
from urllib.parse import urlparse
from urllib.request import Request, urlopen

SOH = 0x01
EOT = 0x04
ACK = 0x06
NAK = 0x15
CAN = 0x18
BLOCK_BYTES = 128
MAX_IMAGE_BYTES = 2 * 1024 * 1024


class FlashError(RuntimeError):
    """An aborted or unsafe firmware transfer."""


class BootloaderTimeout(FlashError):
    """A retryable serial response timeout."""


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = (crc << 1) ^ (0x1021 if crc & 0x8000 else 0)
            crc &= 0xFFFF
    return crc


def encode_packet(block_number: int, payload: bytes) -> bytes:
    if len(payload) > BLOCK_BYTES:
        raise ValueError("XMODEM blocks cannot exceed 128 bytes")
    number = block_number & 0xFF
    block = payload.ljust(BLOCK_BYTES, b"\x1a")
    crc = crc16_xmodem(block)
    return bytes((SOH, number, number ^ 0xFF)) + block + crc.to_bytes(2, "big")


def read_response(connection: socket.socket, expected: set[int], timeout: float) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        connection.settimeout(min(1.0, max(0.01, deadline - time.monotonic())))
        try:
            chunk = connection.recv(1)
        except TimeoutError:
            continue
        if not chunk:
            raise FlashError("TCP stream closed during firmware transfer")
        value = chunk[0]
        if value == CAN:
            raise FlashError("Gecko bootloader cancelled XMODEM")
        if value in expected:
            return value
    raise BootloaderTimeout("Timed out awaiting bootloader response")


def wait_for_prompt(connection: socket.socket, timeout: float = 15.0) -> None:
    deadline = time.monotonic() + timeout
    buffer = bytearray()
    while time.monotonic() < deadline:
        connection.settimeout(min(1.0, max(0.01, deadline - time.monotonic())))
        try:
            chunk = connection.recv(1)
        except TimeoutError:
            continue
        if not chunk:
            raise FlashError("TCP stream closed before bootloader prompt")
        buffer.extend(chunk)
        if b"BL >" in buffer:
            return
        if len(buffer) > 1024:
            del buffer[: len(buffer) - 16]
    raise FlashError("No Gecko 'BL >' prompt (check PA0, reset, UART wiring and mode)")



def wait_for_complete(connection: socket.socket, timeout: float = 60.0) -> None:
    """Do not treat an XMODEM EOT ACK alone as a successful GBL install."""
    deadline = time.monotonic() + timeout
    result = bytearray()
    while time.monotonic() < deadline:
        connection.settimeout(min(1.0, max(0.01, deadline - time.monotonic())))
        try:
            char = connection.recv(1)
        except TimeoutError:
            continue
        if not char:
            raise FlashError("Gecko disconnected before reporting upload completion")
        result.extend(char)
        if len(result) > 4096:
            del result[:1024]
        if b"BL >" in result:
            if b"serial upload complete" in result.lower():
                return
            raise FlashError("Gecko returned to menu without confirming upload success")
    raise FlashError("Gecko did not confirm completion of the GBL image")

def transfer_xmodem(connection: socket.socket, firmware: bytes) -> None:
    if not firmware:
        raise FlashError("Refusing to flash an empty image")
    wait_for_prompt(connection)
    connection.sendall(b"1")  # Gecko UART bootloader menu: upload GBL.
    read_response(connection, {ord("C")}, timeout=20.0)

    total = (len(firmware) + BLOCK_BYTES - 1) // BLOCK_BYTES
    for offset in range(total):
        block = firmware[offset * BLOCK_BYTES : (offset + 1) * BLOCK_BYTES]
        frame = encode_packet(offset + 1, block)
        for attempt in range(10):
            connection.sendall(frame)
            try:
                response = read_response(connection, {ACK, NAK}, timeout=3.0)
            except BootloaderTimeout:
                # Retry timeouts only; a cancelled/closed transfer is terminal.
                response = NAK
            if response == ACK:
                break
        else:
            raise FlashError(f"Block {offset + 1}/{total} failed after 10 attempts")
        if (offset + 1) % 64 == 0 or offset + 1 == total:
            print(f"Transferred {offset + 1}/{total} blocks", flush=True)

    for _ in range(10):
        connection.sendall(bytes((EOT,)))
        try:
            response = read_response(connection, {ACK, NAK}, timeout=5.0)
        except BootloaderTimeout:
            continue
        if response == ACK:
            wait_for_complete(connection)
            return
    raise FlashError("Gecko did not acknowledge XMODEM EOT")


def download(url: str, limit: int = MAX_IMAGE_BYTES) -> bytes:
    if urlparse(url).scheme != "https":
        raise FlashError("Firmware/manifest URLs must use HTTPS")
    request = Request(url, headers={"User-Agent": "serial-radio-gecko-flasher/0.2"})
    with urlopen(request, timeout=30) as response:
        if urlparse(response.url).scheme != "https":
            raise FlashError("Refusing an HTTP downgrade redirect")
        data = response.read(limit + 1)
    if len(data) > limit:
        raise FlashError("Firmware/manifest exceeds the configured maximum size")
    return data


def validate_manifest(raw: bytes) -> dict:
    try:
        document = json.loads(raw)
        url = document["fw_url"]
        expected = document["md5"].lower()
        size = document["size"]
        if not isinstance(url, str) or urlparse(url).scheme != "https":
            raise ValueError("fw_url must be HTTPS")
        if not isinstance(size, int) or not 0 < size <= MAX_IMAGE_BYTES:
            raise ValueError("size out of range")
        if not isinstance(expected, str) or len(expected) != 32 or any(
            char not in "0123456789abcdef" for char in expected
        ):
            raise ValueError("invalid MD5")
        if document.get("baudrate", 115200) != 115200:
            raise ValueError("this board's bootloader UART baud is fixed at 115200")
    except (KeyError, ValueError, TypeError, json.JSONDecodeError) as exc:
        raise FlashError(f"Invalid firmware manifest: {exc}") from exc
    return document


def get_firmware(*, filename: Path | None, manifest_url: str | None) -> bytes:
    if filename is not None:
        firmware = filename.read_bytes()
    elif manifest_url is not None:
        manifest = validate_manifest(download(manifest_url, limit=8192))
        print(f"Manifest firmware version: {manifest.get('version', '(unspecified)')}")
        firmware = download(manifest["fw_url"])
        if len(firmware) != manifest["size"]:
            raise FlashError("Downloaded firmware size does not match manifest")
        if hashlib.md5(firmware, usedforsecurity=False).hexdigest() != manifest["md5"].lower():
            raise FlashError("Downloaded firmware MD5 does not match manifest")
    else:
        raise FlashError("Provide --file or --manifest")
    if not 0 < len(firmware) <= MAX_IMAGE_BYTES:
        raise FlashError("GBL firmware size outside allowed range")
    print(
        f"GBL preflight complete: {len(firmware)} bytes, "
        f"SHA256={hashlib.sha256(firmware).hexdigest()}"
    )
    return firmware


class HomeAssistant:
    def __init__(self, base_url: str, token: str):
        if not token:
            raise FlashError("Missing Home Assistant access token")
        self.base_url = base_url.rstrip("/")
        self.token = token

    def service(self, domain: str, service: str, entity_id: str) -> None:
        if not entity_id.startswith(f"{domain}."):
            raise FlashError(f"Invalid {domain} entity id: {entity_id}")
        payload = json.dumps({"entity_id": entity_id}).encode()
        req = Request(
            f"{self.base_url}/api/services/{domain}/{service}",
            data=payload,
            headers={
                "Authorization": f"Bearer {self.token}",
                "Content-Type": "application/json",
            },
            method="POST",
        )
        with urlopen(req, timeout=15) as response:
            if response.status >= 300:
                raise FlashError(f"Home Assistant service call returned {response.status}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--file", type=Path, help="Pre-downloaded .gbl image")
    source.add_argument("--manifest", help="HTTPS TubeZB flat JSON firmware manifest URL")
    parser.add_argument("--host", required=True, help="ESP32 gateway hostname or IP")
    parser.add_argument("--port", type=int, default=6638)
    parser.add_argument(
        "--confirm-hw-flow", action="store_true",
        help="Confirm the GBL was built for RTS/CTS NCP operation",
    )
    parser.add_argument(
        "--manual", action="store_true", help="Use HA buttons manually; no HA REST calls"
    )
    parser.add_argument("--ha-url", help="Home Assistant base URL, needed unless --manual")
    parser.add_argument(
        "--ha-token-env", default="HA_TOKEN", help="Environment variable containing the HA token"
    )
    parser.add_argument("--arm-entity", help="HA switch entity for Firmware Flash Armed")
    parser.add_argument("--enter-entity", help="HA button entity for Enter Bootloader")
    parser.add_argument("--exit-entity", help="HA button entity for Exit Bootloader")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if not args.confirm_hw_flow:
        raise FlashError(
            "--confirm-hw-flow is required: two-wire NCP images will not work in normal mode"
        )
    image = get_firmware(filename=args.file, manifest_url=args.manifest)
    ha = None
    if not args.manual:
        if not all((args.ha_url, args.arm_entity, args.enter_entity, args.exit_entity)):
            raise FlashError("Automatic mode requires --ha-url and all three HA entity IDs")
        ha = HomeAssistant(args.ha_url, os.environ.get(args.ha_token_env, ""))

    entered = False
    try:
        if ha is not None:
            ha.service("switch", "turn_on", args.arm_entity)
            ha.service("button", "press", args.enter_entity)
            entered = True
        else:
            print("Use HA: stop ZHA, arm firmware, press Enter Bootloader before proceeding.")
        with socket.create_connection((args.host, args.port), timeout=15) as connection:
            transfer_xmodem(connection, image)
        print("XMODEM transfer acknowledged by Gecko bootloader.")
        return 0
    finally:
        if ha is not None:
            try:
                if entered:
                    # The server must see the TCP disconnect before the exit button.
                    time.sleep(0.5)
                    ha.service("button", "press", args.exit_entity)
            finally:
                ha.service("switch", "turn_off", args.arm_entity)
        else:
            print("Use HA: press Exit Bootloader, then disarm firmware maintenance.")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (FlashError, OSError, TimeoutError) as exc:
        print(f"Flashing failed: {exc}", file=sys.stderr)
        sys.exit(1)
