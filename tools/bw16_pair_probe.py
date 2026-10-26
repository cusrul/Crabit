"""BW16 PAIR DIAG probe for the Crabit-Watch UART service.

The probe sends Flutter's 20 + 17 byte PAIR frame, waits for the user to press
the existing BW16 UI's RAM OK button, then confirms re-advertising. The
diagnostic firmware never persists a token.
"""

import argparse
import asyncio
import json
import secrets
import sys
import time
from typing import Optional

from bleak import BleakClient, BleakScanner


SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
DEFAULT_NAME = "Crabit-Watch"
PAIR_WAIT = "PAIR_CONFIRM_ON_WATCH"
PAIR_CONFIRMED = "PAIR_CONFIRMED"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Exercise BW16 PAIR DIAG 20+17 framing and RAM confirmation"
    )
    parser.add_argument("--name", default=DEFAULT_NAME, help="advertised/device name")
    parser.add_argument("--address", help="use a known Windows BLE address instead of scanning")
    parser.add_argument("--scan-seconds", type=float, default=10.0)
    parser.add_argument("--connect-seconds", type=float, default=15.0)
    parser.add_argument("--first-chunk-wait", type=float, default=0.5)
    parser.add_argument("--status-seconds", type=float, default=4.0)
    parser.add_argument("--ram-confirm-seconds", type=float, default=45.0)
    parser.add_argument("--rescan-seconds", type=float, default=10.0)
    parser.add_argument("--token", help="optional exact 32-hex test token")
    return parser.parse_args()


def valid_token(token: str) -> bool:
    return len(token) == 32 and all(character in "0123456789abcdefABCDEF" for character in token)


async def find_device(name: str, address: Optional[str], scan_seconds: float):
    if address:
        print(f"PAIR_TARGET address={address}")
        return address

    print(f"PAIR_SCAN name={name} seconds={scan_seconds:g}")
    devices = await BleakScanner.discover(timeout=scan_seconds)
    for device in devices:
        if device.name == name or device.name == f"{name} ":
            print(f"PAIR_FOUND name={device.name} address={device.address}")
            return device
    print("PAIR_NOT_FOUND")
    return None


def make_notification_handler(buffer: bytearray, statuses: list[str]):
    def on_notification(_characteristic, data: bytearray) -> None:
        buffer.extend(bytes(data))
        while buffer:
            try:
                start = buffer.index(ord("{"))
            except ValueError:
                buffer.clear()
                return
            if start:
                del buffer[:start]
            try:
                event, end = json.JSONDecoder().raw_decode(buffer.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                return
            del buffer[:end]
            topic = str(event.get("t", ""))
            value = str(event.get("v", ""))
            print("PAIR_RX_JSON " + json.dumps(event, ensure_ascii=False, separators=(",", ":")))
            if topic == "sys/ble":
                statuses.append(value)

    return on_notification


async def wait_for(statuses: list[str], expected: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if expected in statuses:
            return True
        await asyncio.sleep(0.05)
    return expected in statuses


async def run_pair_diag(args: argparse.Namespace) -> int:
    device = await find_device(args.name, args.address, args.scan_seconds)
    if device is None:
        return 2

    token = args.token or secrets.token_hex(16)
    command = f"PAIR:{token}".encode("ascii")
    first_chunk = command[:20]
    second_chunk = command[20:]
    if len(command) != 37 or len(first_chunk) != 20 or len(second_chunk) != 17:
        print("PAIR_INVALID_LOCAL_FRAME")
        return 3

    target = device
    address = device if isinstance(device, str) else device.address
    notifications = bytearray()
    statuses: list[str] = []
    print(f"PAIR_CONNECT address={address}")
    try:
        async with BleakClient(target, timeout=args.connect_seconds) as client:
            print(f"PAIR_CONNECTED value={client.is_connected}")
            service = client.services.get_service(SERVICE_UUID)
            tx = client.services.get_characteristic(TX_UUID)
            rx = client.services.get_characteristic(RX_UUID)
            if service is None or tx is None or rx is None:
                print("PAIR_GATT_MISSING")
                return 4

            await client.start_notify(TX_UUID, make_notification_handler(notifications, statuses))
            print(f"PAIR_TX_FIRST len={len(first_chunk)}")
            await client.write_gatt_char(RX_UUID, first_chunk, response=True)
            await asyncio.sleep(max(0.0, args.first_chunk_wait))
            if PAIR_WAIT in statuses or PAIR_CONFIRMED in statuses:
                print(f"PAIR_EARLY_STATE statuses={statuses}")
                return 5
            print("PAIR_FIRST_FRAGMENT_SILENT")

            print(f"PAIR_TX_SECOND len={len(second_chunk)}")
            await client.write_gatt_char(RX_UUID, second_chunk, response=True)
            if not await wait_for(statuses, PAIR_WAIT, args.status_seconds):
                print(f"PAIR_WAIT_MISSING statuses={statuses}")
                return 6
            if PAIR_CONFIRMED in statuses:
                print(f"PAIR_UNEXPECTED_CONFIRM statuses={statuses}")
                return 7
            print("PAIR_DIAG_WAIT_OK action=PRESS_RAM_OK")
            if not await wait_for(statuses, PAIR_CONFIRMED, args.ram_confirm_seconds):
                print(f"PAIR_RAM_CONFIRM_MISSING statuses={statuses}")
                return 8
            print("PAIR_RAM_CONFIRM_OK")
            try:
                await client.stop_notify(TX_UUID)
            except Exception as exc:
                print("PAIR_STOP_NOTIFY_WARNING " f"type={type(exc).__name__} message={exc}")
    except Exception as exc:
        print(f"PAIR_ERROR type={type(exc).__name__} message={exc}")
        return 9

    return 0


async def run_probe(args: argparse.Namespace) -> int:
    if args.token and not valid_token(args.token):
        print("PAIR_INVALID_TOKEN expected=32_hex")
        return 1

    print("PAIR_DIAG_SAFE persistence=false")
    result = await run_pair_diag(args)
    if result:
        return result

    await asyncio.sleep(1.0)
    if await find_device(args.name, args.address, args.rescan_seconds) is None:
        print("PAIR_READVERTISE_MISSING")
        return 10
    print("PAIR_READVERTISE_OK")
    return 0


def main() -> int:
    try:
        return asyncio.run(run_probe(parse_args()))
    except KeyboardInterrupt:
        print("PAIR_INTERRUPTED")
        return 130


if __name__ == "__main__":
    sys.exit(main())
