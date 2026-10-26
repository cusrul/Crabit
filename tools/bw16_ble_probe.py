"""Safe Windows BLE probe for the Crabit-Watch UART service.

The default probe deliberately avoids pairing, bond changes, Wi-Fi commands,
and reset operations. It only scans, connects, subscribes to TX, and sends a
TEST command to RX so the owner/notify path can be observed.
"""

import argparse
import asyncio
import json
import logging
import sys
import time
import traceback
from typing import Optional

from bleak import BleakClient, BleakScanner


SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
DEFAULT_NAME = "Crabit-Watch"
DEFAULT_TEST = "TEST:BLE_PROBE"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Probe Crabit-Watch BLE without pairing")
    parser.add_argument("--name", default=DEFAULT_NAME, help="advertised/device name")
    parser.add_argument("--address", help="use a known Windows BLE address instead of scanning")
    parser.add_argument("--scan-seconds", type=float, default=8.0)
    parser.add_argument("--connect-seconds", type=float, default=12.0)
    parser.add_argument("--keep-seconds", type=float, default=3.0)
    parser.add_argument("--payload", default=DEFAULT_TEST)
    parser.add_argument("--no-write", action="store_true", help="only connect and subscribe")
    parser.add_argument(
        "--debug",
        action="store_true",
        help="log WinRT connection/discovery stages and exception traceback",
    )
    return parser.parse_args()


async def find_device(name: str, address: Optional[str], scan_seconds: float):
    if address:
        print(f"BLE_TARGET address={address}")
        return address

    print(f"BLE_SCAN name={name} seconds={scan_seconds:g}")
    devices = await BleakScanner.discover(timeout=scan_seconds)
    for device in devices:
        if device.name == name or device.name == f"{name} ":
            print(f"BLE_FOUND name={device.name} address={device.address}")
            return device
    print("BLE_NOT_FOUND")
    return None


def make_notification_handler(buffer: bytearray):
    def on_notification(_characteristic, data: bytearray) -> None:
        raw = bytes(data)
        print(f"BLE_RX_CHUNK len={len(raw)} hex={raw.hex()} text={raw!r}")
        buffer.extend(raw)

        while buffer:
            try:
                start = buffer.index(ord("{"))
            except ValueError:
                buffer.clear()
                return
            if start:
                del buffer[:start]
            try:
                decoded = json.JSONDecoder().raw_decode(buffer.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                return
            value, end = decoded
            del buffer[:end]
            print("BLE_RX_JSON " + json.dumps(value, ensure_ascii=False, separators=(",", ":")))

    return on_notification


async def run_probe(args: argparse.Namespace) -> int:
    device = await find_device(args.name, args.address, args.scan_seconds)
    if device is None:
        return 2

    # Keep the discovered BLEDevice object for Windows/WinRT. Re-resolving a
    # random-address peripheral from its string address can time out even
    # though the same scan result is connectable.
    target = device
    address = device if isinstance(device, str) else device.address
    notifications = bytearray()
    started = time.monotonic()

    print(f"BLE_CONNECT address={address}")
    try:
        async with BleakClient(target, timeout=args.connect_seconds) as client:
            print(f"BLE_CONNECTED={client.is_connected}")
            services = client.services
            service = services.get_service(SERVICE_UUID)
            tx = services.get_characteristic(TX_UUID)
            rx = services.get_characteristic(RX_UUID)
            if service is None or tx is None or rx is None:
                print("BLE_GATT_MISSING")
                return 3
            print(f"BLE_GATT_OK service={service.uuid} tx={tx.uuid} rx={rx.uuid}")

            await client.start_notify(TX_UUID, make_notification_handler(notifications))
            print("BLE_NOTIFY_READY")
            if not args.no_write:
                payload = args.payload.encode("utf-8")
                print(f"BLE_TX_TEST len={len(payload)} text={args.payload}")
                # RX is deliberately configured as GATT_CHAR_PROP_WRITE (not
                # WRITE_NO_RSP) in the BW16 owner, so use a write request.
                await client.write_gatt_char(RX_UUID, payload, response=True)
            await asyncio.sleep(max(0.0, args.keep_seconds))
            await client.stop_notify(TX_UUID)
    except Exception as exc:
        if args.debug:
            traceback.print_exc()
        if b"TEST:BLE_PROBE" in notifications:
            elapsed = time.monotonic() - started
            print(f"BLE_CLEANUP_WARNING type={type(exc).__name__} message={exc}")
            print(f"BLE_PROBE_DONE seconds={elapsed:.2f} received_bytes={len(notifications)}")
            return 0
        print(f"BLE_ERROR type={type(exc).__name__} message={exc}")
        return 4

    elapsed = time.monotonic() - started
    print(f"BLE_PROBE_DONE seconds={elapsed:.2f} received_bytes={len(notifications)}")
    return 0


def main() -> int:
    args = parse_args()
    if args.debug:
        # Existing backend logs only; never patch the installed Windows SDK or
        # Bleak package, and do not enable logging for unrelated scanned devices.
        logger = logging.getLogger("bleak.backends.winrt.client")
        handler = logging.StreamHandler()
        handler.setFormatter(logging.Formatter("WINRT %(relativeCreated).0fms %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.DEBUG)
        logger.propagate = False
    try:
        return asyncio.run(run_probe(args))
    except KeyboardInterrupt:
        print("BLE_INTERRUPTED")
        return 130


if __name__ == "__main__":
    sys.exit(main())
