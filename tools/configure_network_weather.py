"""Configure the RAM-only network/weather prototype over Crabit-Watch BLE.

This module has no BLE side effects when imported. BLE is used only by the
explicit command-line entry point, and credentials are never printed or logged.
"""

from __future__ import annotations

import argparse
import asyncio
import codecs
import getpass
import json
import random
import re
import sys
import time
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from typing import Any, Callable


SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
DEFAULT_NAME = "Crabit-Watch"

MAX_GATT_WRITE_BYTES = 20
MAX_NOTIFICATION_BUFFER_BYTES = 4096
MAX_SSID_BYTES = 32
MAX_PASSWORD_BYTES = 64
MIN_TZ_MINUTES = -12 * 60
MAX_TZ_MINUTES = 14 * 60
MAX_FORECAST_DAYS = 7

SCAN_TIMEOUT_SECONDS = 8.0
CONNECT_TIMEOUT_SECONDS = 12.0
GATT_WRITE_TIMEOUT_SECONDS = 5.0
GATT_CHUNK_GAP_SECONDS = 0.03
AUTH_RESPONSE_TIMEOUT_SECONDS = 12.0
APP_RESPONSE_TIMEOUT_SECONDS = 15.0

_JSON_DECODER = json.JSONDecoder()
_AUTH_CODE_RE = re.compile(r"[0-9]{6}\Z")
_READBACK_RE = re.compile(r"configured=1;days=([1-7])\Z")


class ConfigurationError(Exception):
    """A safe-to-display configuration or protocol error."""


def _utf8_bytes(value: str, label: str, maximum: int, minimum: int = 0) -> bytes:
    if not isinstance(value, str):
        raise ConfigurationError(f"{label} must be text.")
    try:
        encoded = value.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise ConfigurationError(f"{label} is not valid UTF-8 text.") from exc
    if len(encoded) < minimum or len(encoded) > maximum:
        if minimum == maximum:
            expectation = f"exactly {maximum} UTF-8 bytes"
        elif minimum:
            expectation = f"between {minimum} and {maximum} UTF-8 bytes"
        else:
            expectation = f"at most {maximum} UTF-8 bytes"
        raise ConfigurationError(f"{label} must use {expectation}.")
    return encoded


def validate_ssid(ssid: str) -> bytes:
    """Return the SSID's UTF-8 bytes after enforcing the 802.11 byte limit."""

    return _utf8_bytes(ssid, "SSID", MAX_SSID_BYTES, minimum=1)


def validate_password(password: str) -> bytes:
    """Return the password's UTF-8 bytes without imposing a WPA policy."""

    return _utf8_bytes(password, "Wi-Fi password", MAX_PASSWORD_BYTES)


def validate_auth_code(auth_code: str) -> None:
    if not _AUTH_CODE_RE.fullmatch(auth_code):
        raise ConfigurationError("The BLE authentication code must be six digits.")


def encode_credential_setting(field: str, value: str) -> str:
    """Encode a credential as the prototype's field=<UTF-8 hex> value."""

    if field == "ssid":
        raw = validate_ssid(value)
    elif field == "pass":
        raw = validate_password(value)
    else:
        raise ConfigurationError("Unsupported credential field.")
    return f"{field}={raw.hex()}"


def coordinate_to_e6(value: str | float | Decimal, label: str, low: int, high: int) -> int:
    try:
        coordinate = Decimal(str(value))
    except (InvalidOperation, ValueError):
        raise ConfigurationError(f"{label} must be a finite number.") from None
    if not coordinate.is_finite():
        raise ConfigurationError(f"{label} must be a finite number.")
    if coordinate < low or coordinate > high:
        raise ConfigurationError(f"{label} must be between {low} and {high} degrees.")
    return int((coordinate * Decimal(1_000_000)).quantize(Decimal("1"), rounding=ROUND_HALF_UP))


def build_place_setting(
    latitude: str | float | Decimal, longitude: str | float | Decimal, tz_minutes: int, days: int
) -> str:
    lat_e6 = coordinate_to_e6(latitude, "Latitude", -90, 90)
    lon_e6 = coordinate_to_e6(longitude, "Longitude", -180, 180)
    if isinstance(tz_minutes, bool) or not isinstance(tz_minutes, int):
        raise ConfigurationError("Time-zone offset must be an integer number of minutes.")
    if not MIN_TZ_MINUTES <= tz_minutes <= MAX_TZ_MINUTES:
        raise ConfigurationError("Time-zone offset must be between -720 and 840 minutes.")
    if isinstance(days, bool) or not isinstance(days, int) or not 1 <= days <= MAX_FORECAST_DAYS:
        raise ConfigurationError("Forecast days must be an integer from 1 through 7.")
    return f"place={lat_e6},{lon_e6},{tz_minutes},{days}"


def make_network_request(request_id: int, value: str) -> bytes:
    if (
        isinstance(request_id, bool)
        or not isinstance(request_id, int)
        or not 1 <= request_id <= 0xFFFF
    ):
        raise ConfigurationError("Request id must be a nonzero 16-bit integer.")
    message = {"i": request_id, "t": "app/net", "v": value}
    return json.dumps(message, ensure_ascii=True, separators=(",", ":")).encode("utf-8")


def chunk_payload(payload: bytes, chunk_size: int = MAX_GATT_WRITE_BYTES) -> list[bytes]:
    if not isinstance(payload, bytes):
        raise ConfigurationError("BLE payload must be bytes.")
    if chunk_size < 1 or chunk_size > MAX_GATT_WRITE_BYTES:
        raise ConfigurationError("BLE write chunks must be between 1 and 20 bytes.")
    return [payload[offset : offset + chunk_size] for offset in range(0, len(payload), chunk_size)]


class JsonObjectStream:
    """Decode a bounded UTF-8 stream containing adjacent JSON objects."""

    def __init__(self, maximum_buffer_bytes: int = MAX_NOTIFICATION_BUFFER_BYTES) -> None:
        self._utf8 = codecs.getincrementaldecoder("utf-8")("strict")
        self._decoder = json.JSONDecoder()
        self._buffer = ""
        self._maximum_buffer_bytes = maximum_buffer_bytes

    def _check_bound(self) -> None:
        pending_bytes = self._utf8.getstate()[0]
        buffered_bytes = len(self._buffer.encode("utf-8")) + len(pending_bytes)
        if buffered_bytes > self._maximum_buffer_bytes:
            raise ConfigurationError("BLE notification exceeded the bounded JSON buffer.")

    def feed(self, chunk: bytes) -> list[dict[str, Any]]:
        try:
            self._buffer += self._utf8.decode(chunk, final=False)
        except UnicodeDecodeError as exc:
            raise ConfigurationError("BLE notification was not valid UTF-8.") from exc
        self._check_bound()

        messages: list[dict[str, Any]] = []
        while self._buffer:
            leading_space = len(self._buffer) - len(self._buffer.lstrip())
            if leading_space:
                self._buffer = self._buffer[leading_space:]
            if not self._buffer:
                break
            if self._buffer[0] != "{":
                raise ConfigurationError("BLE notification did not contain a JSON object.")
            try:
                value, end = self._decoder.raw_decode(self._buffer)
            except json.JSONDecodeError:
                # raw_decode cannot distinguish every invalid document from a
                # valid prefix split at a notification boundary (notably an
                # unterminated JSON string). Retain it under the strict byte
                # bound; the request's bounded response wait handles no-reply.
                break
            if not isinstance(value, dict):
                raise ConfigurationError("BLE notification JSON must be an object.")
            messages.append(value)
            self._buffer = self._buffer[end:]
            self._check_bound()
        return messages


def is_auth_ack(message: dict[str, Any]) -> bool:
    # RadioSvc.cpp emits this exact, uncorrelated acknowledgment after AUTH.
    return message.get("t") == "auth" and message.get("v") == "1"


def is_correlated_response(
    message: dict[str, Any], request_id: int, topic: str = "app/net"
) -> bool:
    response_id = message.get("i")
    return type(response_id) is int and response_id == request_id and message.get("t") == topic


def validate_config_ack(value: Any) -> None:
    raise_if_busy(value)
    if value != "OK":
        raise ConfigurationError("The watch did not accept a network configuration stage.")


def raise_if_busy(value: Any) -> None:
    if isinstance(value, str) and "BUSY" in value.upper():
        raise ConfigurationError(
            "The watch reported BLE/network busy. Close the other BLE client and try again."
        )


def parse_readback(value: Any, requested_days: int) -> int:
    if not isinstance(value, str):
        raise ConfigurationError("The watch returned an invalid configuration readback.")
    match = _READBACK_RE.fullmatch(value)
    if not match:
        raise ConfigurationError("The watch did not confirm a configured network.")
    days = int(match.group(1))
    if days != requested_days:
        raise ConfigurationError("The watch readback did not match the requested forecast days.")
    return days


class RequestIds:
    def __init__(self, first: int | None = None) -> None:
        self._next = first if first is not None else random.SystemRandom().randint(1, 0xFFFF)
        if not 1 <= self._next <= 0xFFFF:
            raise ConfigurationError("Initial request id must be a nonzero 16-bit integer.")

    def next(self) -> int:
        value = self._next
        self._next = 1 if value == 0xFFFF else value + 1
        return value


class NotificationInbox:
    """Bounded async handoff from Bleak notifications to the protocol reader."""

    def __init__(self) -> None:
        self._loop = asyncio.get_running_loop()
        self._stream = JsonObjectStream()
        self._messages: asyncio.Queue[dict[str, Any] | None] = asyncio.Queue(maxsize=128)
        self._failure: str | None = None

    def callback(self, _characteristic: Any, data: bytearray) -> None:
        self._loop.call_soon_threadsafe(self._consume, bytes(data))

    def _fail(self, message: str) -> None:
        self._failure = message
        while not self._messages.empty():
            self._messages.get_nowait()
        self._messages.put_nowait(None)

    def _consume(self, data: bytes) -> None:
        if self._failure is not None:
            return
        try:
            decoded = self._stream.feed(data)
        except ConfigurationError:
            self._fail(
                "BLE notification parsing failed safely; no credential values were displayed."
            )
            return
        for message in decoded:
            if self._messages.full():
                self._fail(
                    "Too many BLE notifications arrived before the active request completed."
                )
                return
            self._messages.put_nowait(message)

    async def next_message(self, timeout: float) -> dict[str, Any]:
        if self._failure is not None:
            raise ConfigurationError(self._failure)
        try:
            message = await asyncio.wait_for(self._messages.get(), timeout=timeout)
        except asyncio.TimeoutError as exc:
            raise ConfigurationError("Timed out waiting for a BLE response.") from exc
        if message is None:
            raise ConfigurationError(self._failure or "BLE notification processing failed.")
        return message


async def send_ble_payload(client: Any, payload: bytes) -> None:
    chunks = chunk_payload(payload)
    for index, chunk in enumerate(chunks):
        await asyncio.wait_for(
            client.write_gatt_char(RX_UUID, chunk, response=True),
            timeout=GATT_WRITE_TIMEOUT_SECONDS,
        )
        if index + 1 < len(chunks):
            await asyncio.sleep(GATT_CHUNK_GAP_SECONDS)


async def wait_for_message(
    inbox: NotificationInbox, predicate: Callable[[dict[str, Any]], bool], timeout: float
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ConfigurationError("Timed out waiting for a correlated BLE response.")
        message = await inbox.next_message(remaining)
        if predicate(message):
            return message


async def request_network_value(
    client: Any, inbox: NotificationInbox, request_ids: RequestIds, value: str
) -> dict[str, Any]:
    request_id = request_ids.next()
    await send_ble_payload(client, make_network_request(request_id, value))
    return await wait_for_message(
        inbox,
        lambda message: is_correlated_response(message, request_id),
        APP_RESPONSE_TIMEOUT_SECONDS,
    )


async def send_network_stage(
    client: Any, inbox: NotificationInbox, request_ids: RequestIds, value: str, stage_name: str
) -> dict[str, Any]:
    response = await request_network_value(client, inbox, request_ids, value)
    validate_config_ack(response.get("v"))
    print(f"{stage_name} accepted.")
    return response


async def configure_watch(
    address: str | None,
    ssid_value: str,
    password_value: str,
    place_value: str,
    requested_days: int,
    auth_code: str,
) -> None:
    # Import only for an explicitly invoked BLE operation; module import and
    # --help remain usable without Bleak installed and never contact a device.
    from bleak import BleakClient, BleakScanner

    target: Any = address
    if target is None:
        discovered = await BleakScanner.discover(timeout=SCAN_TIMEOUT_SECONDS)
        target = next(
            (device for device in discovered if (device.name or "").rstrip() == DEFAULT_NAME), None
        )
        if target is None:
            raise ConfigurationError("No Crabit-Watch BLE device was found.")

    async with BleakClient(target, timeout=CONNECT_TIMEOUT_SECONDS) as client:
        services = client.services
        if (
            services.get_service(SERVICE_UUID) is None
            or services.get_characteristic(TX_UUID) is None
            or services.get_characteristic(RX_UUID) is None
        ):
            raise ConfigurationError("The connected device is missing the Crabit UART BLE service.")

        inbox = NotificationInbox()
        await asyncio.wait_for(
            client.start_notify(TX_UUID, inbox.callback),
            timeout=GATT_WRITE_TIMEOUT_SECONDS,
        )
        print("BLE connected; notifications enabled.")

        try:
            await send_ble_payload(client, f"AUTH:{auth_code}".encode("ascii"))
            await wait_for_message(inbox, is_auth_ack, AUTH_RESPONSE_TIMEOUT_SECONDS)
            print("Authentication accepted.")

            request_ids = RequestIds()
            await send_network_stage(
                client,
                inbox,
                request_ids,
                encode_credential_setting("ssid", ssid_value),
                "SSID",
            )
            await send_network_stage(
                client,
                inbox,
                request_ids,
                encode_credential_setting("pass", password_value),
                "Password",
            )
            await send_network_stage(
                client,
                inbox,
                request_ids,
                place_value,
                "Location/time",
            )
            readback = await request_network_value(client, inbox, request_ids, "read")
            readback_value = readback.get("v")
            raise_if_busy(readback_value)
            configured_days = parse_readback(readback_value, requested_days)
            print("Configuration readback accepted.")
            print(f"Readback: configured=1; days={configured_days}.")
        finally:
            try:
                await asyncio.wait_for(
                    client.stop_notify(TX_UUID), timeout=GATT_WRITE_TIMEOUT_SECONDS
                )
            except Exception:
                # Closing the BLE context below remains the cleanup boundary.
                pass

    print("BLE session closed; you can now open the weather app.")


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Configure the Crabit-Watch RAM-only weather network settings over authenticated BLE."
    )
    parser.add_argument("--ssid", help="Wi-Fi SSID (prompted if omitted)")
    parser.add_argument("--latitude", required=True, help="Latitude in degrees, from -90 to 90")
    parser.add_argument("--longitude", required=True, help="Longitude in degrees, from -180 to 180")
    parser.add_argument(
        "--tz-minutes",
        type=int,
        default=480,
        help="UTC offset in minutes, from -720 to 840 (default: 480)",
    )
    parser.add_argument(
        "--days", type=int, default=3, help="Forecast days from 1 to 7 (default: 3)"
    )
    parser.add_argument("--address", help="Known BLE address; otherwise scan for Crabit-Watch")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_argument_parser()
    arguments = list(sys.argv[1:] if argv is None else argv)
    if any(item == "--password" or item.startswith("--password=") for item in arguments):
        print(
            "Error: enter the Wi-Fi password at the hidden prompt, not on the command line.",
            file=sys.stderr,
        )
        return 2
    args = parser.parse_args(arguments)
    try:
        place_value = build_place_setting(args.latitude, args.longitude, args.tz_minutes, args.days)
        ssid_value = args.ssid if args.ssid is not None else input("Wi-Fi SSID: ")
        validate_ssid(ssid_value)
        password_value = getpass.getpass("Wi-Fi password (input hidden): ")
        validate_password(password_value)
        auth_code = getpass.getpass("Six-digit BLE authentication code (input hidden): ")
        validate_auth_code(auth_code)
        asyncio.run(
            configure_watch(
                args.address, ssid_value, password_value, place_value, args.days, auth_code
            )
        )
    except ConfigurationError as exc:
        print(f"Configuration failed: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Cancelled; no reset or flash operation was performed.", file=sys.stderr)
        return 130
    except Exception:
        print("BLE operation failed. Credential contents were not displayed.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
