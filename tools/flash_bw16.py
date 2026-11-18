#!/usr/bin/env python3
"""Paired BW16 two-range uploader; dry-run unless --execute is explicit.

Preserves the locally validated prototype's protocol and erase order. It does
not reset/power the watch or change the STM32 bridge. OTA2 is retired; there is
no A/B rollback. Use only with an operator-approved board and recovery path.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import time

try:
    import serial
except ImportError:
    serial = None

SYNC, ACK = 0x15, 0x06
FLASHLOADER_ADDR = 0x082000
MAIN_ADDR, MAIN_MAX = 0x08006000, 0xF4000
EXT_ADDR, EXT_END = 0x08107000, 0x081F8000
OTA2_ADDR, OTA2_SIZE = 0x08106000, 0x1000


def default_flashloader() -> Path:
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    return (
        local
        / "Arduino15/packages/realtek/tools/ameba_d_tools/1.1.3/imgtool_flashloader_amebad.bin"
    )


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def uint8_checksum(data: bytes) -> int:
    return (0xFF + sum(data)) & 0xFF


def word_checksum(data: bytes) -> int:
    if len(data) % 4:
        raise ValueError("Checksum input must be word-aligned")
    return sum(word[0] for word in struct.iter_unpack("<I", data)) & 0xFFFFFFFF


class FlashError(RuntimeError):
    pass


class ImagePair:
    """Read and validate once before opening a port; flash these exact bytes."""

    def __init__(self, main: Path, extension: Path, report: Path, flashloader: Path):
        if main.resolve() == extension.resolve():
            raise ValueError("Main and extension must be different files")
        self.main, self.extension, self.loader = [
            p.read_bytes() for p in (main, extension, flashloader)
        ]
        for label, data, capacity in (
            ("main", self.main, MAIN_MAX),
            ("extension", self.extension, EXT_END - EXT_ADDR),
        ):
            if not 0 < len(data) <= capacity or len(data) % 4:
                raise ValueError(
                    f"Invalid {label} length 0x{len(data):X}: must fit and be word-aligned"
                )
        if not self.loader:
            raise ValueError("Empty flashloader")
        evidence = json.loads(report.read_text(encoding="utf-8-sig"))
        if not isinstance(evidence, dict) or evidence.get("status") != "PASS":
            raise ValueError(
                "Build report must have status PASS (build evidence, not hardware approval)"
            )
        expected = {
            "main_bytes": len(self.main),
            "extension_bytes": len(self.extension),
            "main_sha256": digest(self.main),
            "extension_sha256": digest(self.extension),
        }
        for key, value in expected.items():
            if evidence.get(key) != value:
                raise ValueError(f"Build report mismatch: {key}")
        addresses = {
            "main_physical_end": MAIN_ADDR - 0x08000000 + len(self.main),
            "extension_offset": EXT_ADDR - 0x08000000,
            "extension_end": EXT_ADDR - 0x08000000 + len(self.extension),
        }
        for key, value in addresses.items():
            reported = evidence.get(key)
            try:
                actual = int(str(reported), 0)
            except ValueError:
                actual = None
            if actual != value:
                raise ValueError(
                    f"Build report mismatch: {key} (report {reported!r}, expected 0x{value:X})"
                )

    def describe(self):
        for label, address, data in (
            ("main", MAIN_ADDR, self.main),
            ("extension", EXT_ADDR, self.extension),
        ):
            end = address + len(data)
            print(f"{label}: 0x{address:08X}..0x{end:08X}, {len(data)} B, SHA256 {digest(data)}")
        print(f"flashloader: {len(self.loader)} B, SHA256 {digest(self.loader)}")
        print("Erase order: OTA2 signature sector, main, extension. No A/B fallback.")
        print("Protected 0x080FA000..0x08106000 untouched; no boot image writes.")


class Protocol:
    def __init__(self, port: str, baud: int = 115200):
        if serial is None:
            raise FlashError("pyserial is required; install it in your selected Python environment")
        self.ser = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=8,
            parity=serial.PARITY_NONE,
            stopbits=1,
            timeout=0.05,
            write_timeout=10,
        )
        self.action = "initial sync"

    def close(self):
        try:
            self.ser.close()
        except OSError as error:
            # A failing close must not replace the original flash error or its exit code.
            print(f"Warning: serial close failed: {error}", file=sys.stderr)

    def read_byte(self):
        data = self.ser.read(1)
        return data[0] if data else -1

    def write(self, data: bytes):
        if self.ser.write(data) != len(data):
            raise FlashError(f"Short serial write during {self.action}")

    def wait_sync(self, count=1, timeout=10):
        deadline = time.monotonic() + timeout
        remaining = count
        while time.monotonic() < deadline:
            byte = self.read_byte()
            if byte == SYNC:
                remaining -= 1
                if remaining == 0:
                    return True
            elif byte != -1:
                remaining = count
        return False

    def wait_ack(self, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            byte = self.read_byte()
            if byte == ACK:
                return True
            if byte not in (-1, SYNC):
                raise FlashError(f"Unexpected response 0x{byte:02X} during {self.action}")
        return False

    def send(self, data: bytes, ack_timeout=10):
        if data[0] != 0x07 and not self.wait_sync():
            raise FlashError(f"Sync timeout during {self.action}")
        self.write(data)
        if not self.wait_ack(ack_timeout):
            raise FlashError(f"ACK timeout during {self.action}")

    def handshake(self):
        self.send(bytes([0x07]))
        if not self.wait_sync():
            raise FlashError(f"Handshake sync timeout during {self.action}")

    def write_block(self, address: int, data: bytes, seq_id: int, label: str):
        chunks = (len(data) + 1023) // 1024
        padded = data + b"\xFF" * (chunks * 1024 - len(data))
        self.action = f"{label} write sync"
        if not self.wait_sync():
            raise FlashError(f"Sync timeout during {self.action}")
        for index in range(chunks):
            self.action = f"{label} chunk {index + 1}/{chunks} at 0x{address + index * 1024:08X}"
            packet = bytearray(1032)
            packet[0:3] = bytes([0x02, seq_id & 0xFF, (~seq_id) & 0xFF])
            struct.pack_into("<I", packet, 3, address + index * 1024)
            packet[7:1031] = padded[index * 1024 : (index + 1) * 1024]
            packet[1031] = uint8_checksum(packet[:1031])
            self.write(packet)
            if not self.wait_ack():
                raise FlashError(f"ACK timeout during {self.action}")
            seq_id = (seq_id + 1) & 0xFF
        print(f"  wrote {label}: {len(data)} bytes", flush=True)
        return seq_id

    def erase_block(self, address: int, size: int, label: str):
        self.action = f"erase {label} at 0x{address:08X}"
        sectors = (size + 4095) // 4096
        # Reference protocol uses a 24-bit offset and a 16-bit sector count.
        command = (
            bytes([0x17])
            + (address & 0xFFFFFF).to_bytes(3, "little")
            + sectors.to_bytes(2, "little")
        )
        self.send(command, ack_timeout=120)
        print(f"  erased {label}: 0x{address:08X}, {sectors} sectors", flush=True)

    def verify(self, address: int, data: bytes, label: str):
        self.action = f"verify {label} at 0x{address:08X}"
        if not self.wait_sync():
            raise FlashError(f"Sync timeout during {self.action}")
        # Preserve the prototype's 7-byte command: 24-bit offset and length.
        command = (
            bytes([0x27])
            + (address & 0xFFFFFF).to_bytes(3, "little")
            + len(data).to_bytes(3, "little")
        )
        self.write(command)
        result = bytearray()
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            byte = self.read_byte()
            if byte == -1 or (not result and byte == SYNC):
                continue
            if not result and byte != 0x27:
                raise FlashError(f"Unexpected verify response 0x{byte:02X}")
            result.append(byte)
            if len(result) == 5:
                break
        if len(result) != 5:
            raise FlashError(f"Response timeout during {self.action}")
        actual = struct.unpack("<I", result[1:])[0]
        expected = word_checksum(data)
        if actual != expected:
            raise FlashError(
                f"Checksum mismatch {label}: got 0x{actual:08X}, expected 0x{expected:08X}"
            )
        print(f"  verified {label}: checksum 0x{actual:08X}", flush=True)


def execute(pair: ImagePair, port: str, wait: float):
    protocol = None
    destructive = False
    try:
        protocol = Protocol(port)
        if wait:
            # Operator may still be entering download mode; name this phase so an
            # interrupt is not reported as a sync timeout.
            protocol.action = f"waiting {wait:g}s before first sync"
            time.sleep(wait)
            protocol.action = "initial sync"
        print(f"Waiting for download-mode sync on {port} at 115200...", flush=True)
        if not protocol.wait_sync(2):
            raise FlashError("No sync; enter BW16 download mode")
        protocol.handshake()
        protocol.write_block(FLASHLOADER_ADDR, pair.loader, 1, "flashloader")
        protocol.action = "execute flashloader"
        protocol.send(bytes([0x04]))
        if not protocol.wait_sync(2):
            raise FlashError("No sync after flashloader")
        protocol.action = "SPI flash init"
        protocol.send(bytes([0x26, 0x01, 0x01, 0x00]))
        # Set before sending: a lost ACK cannot prove an erase did not happen.
        destructive = True
        protocol.erase_block(OTA2_ADDR, OTA2_SIZE, "OTA2 signature sector")
        protocol.erase_block(MAIN_ADDR, len(pair.main), "main image")
        protocol.erase_block(EXT_ADDR, len(pair.extension), "extension")
        protocol.action = "post-erase handshake"
        if not protocol.wait_sync():
            raise FlashError("No sync after erase")
        protocol.handshake()
        seq = protocol.write_block(MAIN_ADDR, pair.main, 1, "main image")
        protocol.write_block(EXT_ADDR, pair.extension, seq, "extension")
        protocol.action = "finish write"
        protocol.send(bytes([0x04]))
        if not protocol.wait_sync():
            raise FlashError("No sync before verify")
        protocol.verify(MAIN_ADDR, pair.main, "main image")
        protocol.verify(EXT_ADDR, pair.extension, "extension")
        print("PASS: main and extension written and checksum-verified.")
        print("Exit the flash bridge manually; this tool does not reset or power the device.")
        return 0
    except (FlashError, OSError, KeyboardInterrupt) as error:
        action = protocol.action if protocol else "open serial port"
        print(f"FAILED during {action}: {error or 'interrupted'}", file=sys.stderr)
        if destructive:
            print(
                "Flash erase/write may be incomplete; do not assume BW16 can boot. "
                "Manually re-enter the STM32 flash bridge/download mode, then rerun "
                "the SAME verified pair. No automatic retry or reset was attempted.",
                file=sys.stderr,
            )
        else:
            print("No flash erase command was issued by this attempt.", file=sys.stderr)
        return 1
    finally:
        if protocol:
            protocol.close()


def probe(port: str, seconds: float) -> int:
    protocol = Protocol(port)
    try:
        deadline = time.monotonic() + seconds
        syncs = 0
        while time.monotonic() < deadline:
            if protocol.read_byte() == SYNC:
                syncs += 1
        print(f"sync 0x15 count: {syncs} (listen only; no bytes sent)")
        if not syncs:
            print(
                "No sync seen; enter BW16 download mode and retry. Nothing was sent.",
                file=sys.stderr,
            )
        return 0 if syncs else 1
    finally:
        protocol.close()


def nonnegative_seconds(value: str) -> float:
    result = float(value)
    if not math.isfinite(result) or result < 0:
        raise argparse.ArgumentTypeError("Expected finite seconds >= 0")
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--execute", action="store_true")
    mode.add_argument("--probe", action="store_true", help="Listen only; no bytes sent")
    parser.add_argument("--main", type=Path)
    parser.add_argument("--extension", type=Path)
    parser.add_argument("--report", type=Path, help="Matching split-build report.json")
    parser.add_argument("--flashloader", type=Path, default=default_flashloader())
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, choices=[115200], default=115200)
    parser.add_argument("--wait", type=nonnegative_seconds, default=0)
    parser.add_argument("--seconds", type=nonnegative_seconds, default=6)
    parser.add_argument("--retire-ota2", action="store_true")
    args = parser.parse_args(argv)
    if (args.execute or args.probe) and not args.port:
        parser.error("--port is required for --execute/--probe")
    if args.execute and not args.retire_ota2:
        parser.error("--execute requires --retire-ota2 (no A/B fallback)")
    try:
        if args.probe:
            return probe(args.port, args.seconds)
        if not all((args.main, args.extension, args.report)):
            parser.error("--main, --extension and --report are required")
        pair = ImagePair(args.main, args.extension, args.report, args.flashloader)
        pair.describe()
        if not args.execute:
            print("DRY RUN: pair validated; no serial port opened. Not hardware acceptance.")
            return 0
        return execute(pair, args.port, args.wait)
    except (ValueError, OSError, FlashError) as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
