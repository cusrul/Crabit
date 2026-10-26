"""Bounded host-side probe for the Crabit CDC + keyboard-HID prototype."""

from __future__ import annotations

import argparse
import re
import secrets
import sys
import time
from typing import Callable


REQUEST_TIMEOUT = 2.0
MAX_BUFFER = 4096
MAX_LINE = 256
ECHO_COUNT = 20
TYPE_TEXT = "crabit usb test\n"
STATUS_RE = re.compile(
    rb"^CRABIT:USB:STATUS ([0-9A-Fa-f]{8}) " rb"cdc=1 hid=1 test=(IDLE|RUNNING|DONE|FAILED)$"
)


class ProbeError(RuntimeError):
    pass


class USBCompositeProbe:
    def __init__(
        self,
        serial_port,
        *,
        nonce_factory: Callable[[], str] | None = None,
        clock: Callable[[], float] = time.monotonic,
    ):
        self.serial = serial_port
        self._nonce_factory = nonce_factory or (lambda: secrets.token_hex(4).upper())
        self._clock = clock
        self._buffer = bytearray()

    def _nonce(self) -> str:
        nonce = self._nonce_factory()
        if not re.fullmatch(r"[0-9A-Fa-f]{8}", nonce):
            raise ProbeError("nonce generator must return exactly eight hexadecimal characters")
        return nonce.upper()

    def _line(self, deadline: float) -> bytes:
        while True:
            if self._clock() >= deadline:
                raise ProbeError("timed out waiting for matching USB reply")
            newline = self._buffer.find(b"\n")
            if newline >= 0:
                line = bytes(self._buffer[:newline]).rstrip(b"\r")
                del self._buffer[: newline + 1]
                if len(line) > MAX_LINE:
                    continue
                return line
            if len(self._buffer) >= MAX_BUFFER:
                self._buffer.clear()
                raise ProbeError("serial receive buffer exceeded limit")
            remaining = deadline - self._clock()
            if remaining <= 0:
                raise ProbeError("timed out waiting for matching USB reply")
            self.serial.timeout = min(remaining, 0.1)
            chunk = self.serial.read(min(256, MAX_BUFFER - len(self._buffer)))
            if chunk:
                self._buffer.extend(chunk)

    def _request(
        self, command: str, expected: Callable[[bytes], bool], timeout: float = REQUEST_TIMEOUT
    ) -> bytes:
        if not command.isascii() or len(command.encode("ascii")) > 64 or not command.endswith("\n"):
            raise ProbeError("internal request violates the ASCII/64-byte/newline contract")
        deadline = self._clock() + timeout
        encoded = command.encode("ascii")
        if self.serial.write(encoded) != len(encoded):
            raise ProbeError("incomplete serial write; request will not be replayed")
        while True:
            line = self._line(deadline)
            if line.startswith(b"CRABIT:USB:ERROR "):
                raise ProbeError(line.decode("ascii", errors="replace"))
            if expected(line):
                return line

    def status(self, *, deadline: float | None = None) -> tuple[str, str]:
        nonce = self._nonce()
        line = self._request(
            f"CRABIT:USB:STATUS {nonce}\n",
            lambda value: bool(STATUS_RE.fullmatch(value))
            and value.split(b" ")[1].decode().upper() == nonce,
            timeout=(
                REQUEST_TIMEOUT
                if deadline is None
                else max(0.0, min(REQUEST_TIMEOUT, deadline - self._clock()))
            ),
        )
        match = STATUS_RE.fullmatch(line)
        assert match is not None
        return match.group(1).decode().upper(), match.group(2).decode()

    def echo(self, *, deadline: float | None = None) -> None:
        nonce = self._nonce()
        self._request(
            f"CRABIT:USB:ECHO {nonce}\n",
            lambda line: line == f"CRABIT:USB:ECHO {nonce}".encode("ascii"),
            timeout=(
                REQUEST_TIMEOUT
                if deadline is None
                else max(0.0, min(REQUEST_TIMEOUT, deadline - self._clock()))
            ),
        )

    def type_fixed_text(self) -> str:
        nonce = self._nonce()
        response = self._request(
            f"CRABIT:USB:TYPE {nonce}\n",
            lambda line: line
            in {
                f"CRABIT:USB:TYPE {nonce} QUEUED".encode("ascii"),
                f"CRABIT:USB:TYPE {nonce} BUSY".encode("ascii"),
                f"CRABIT:USB:TYPE {nonce} REJECTED".encode("ascii"),
            },
        )
        result = response.rsplit(b" ", 1)[1].decode("ascii")
        if result != "QUEUED":
            raise ProbeError(f"fixed-text request was {result.lower()}")
        end = self._clock() + 5.0
        for _ in range(ECHO_COUNT):
            if self._clock() >= end:
                raise ProbeError("timed out during concurrent CDC echoes")
            self.echo(deadline=end)
        while True:
            if self._clock() >= end:
                raise ProbeError("timed out polling keyboard-HID test status")
            _, state = self.status(deadline=end)
            if state in ("DONE", "FAILED"):
                if state != "DONE":
                    raise ProbeError("device reported keyboard-HID test failure")
                return state
            if self._clock() >= end:
                raise ProbeError("timed out polling keyboard-HID test status")
            time.sleep(min(0.1, max(0.0, end - self._clock())))


def run(
    port: str,
    *,
    do_type: bool = False,
    confirm_target: bool = False,
    serial_factory=None,
    output=print,
) -> None:
    if do_type and not confirm_target:
        raise ProbeError("--type-fixed-text requires --confirm-target")
    if serial_factory is None:
        try:
            import serial
        except ImportError as exc:
            raise ProbeError("pyserial is required for a real port; install it separately") from exc
        serial_factory = serial.Serial
    with serial_factory(
        port=port, baudrate=115200, timeout=0.1, write_timeout=REQUEST_TIMEOUT
    ) as connection:
        probe = USBCompositeProbe(connection)
        _, state = probe.status()
        output(f"CDC/HID composite status: test={state}")
        for index in range(ECHO_COUNT):
            probe.echo()
            output(f"serial echo {index + 1}/{ECHO_COUNT}: PASS")
        if do_type:
            output(f"Requesting fixed keyboard text: {TYPE_TEXT!r}")
            probe.type_fixed_text()
            output("device HID test reported DONE; keyboard delivery still requires UI observation")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="explicit serial port (for example COM5)")
    parser.add_argument(
        "--type-fixed-text", action="store_true", help="send only the built-in harmless test string"
    )
    parser.add_argument(
        "--confirm-target",
        action="store_true",
        help="confirm that the explicitly selected target is safe for typing",
    )
    args = parser.parse_args(argv)
    try:
        run(args.port, do_type=args.type_fixed_text, confirm_target=args.confirm_target)
    except (ProbeError, OSError) as exc:
        print(f"USB probe failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
