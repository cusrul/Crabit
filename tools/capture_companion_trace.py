"""Passive CDC capture. No device commands, port probing, flash or reset."""

import argparse
from datetime import datetime
from pathlib import Path
import re
import time

TRACE = re.compile(rb"CRABIT:RADIO t=\d+ sid=\d+ ev=[A-Z_]+ a=\d+ b=\d+ lost=\d+")
NETWORK_TRACE = re.compile(
    rb"\[NET\] id=\d+ (?:stage=[A-Z_:]+|progress=[A-Z_]+ rtc=[01]|"
    rb"result=[A-Z_]+ last=[A-Z_]+ rtc=[01] days=\d+|"
    rb"rtc_time=-?\d+ rtc_date=-?\d+ local=\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}|"
    rb"rtc_read=-?\d+,-?\d+ value=\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}|"
    rb"bytes=\d+ ms=\d+ ok=[01]|http=\d+|ntp=\d+ reason=[012] epoch=\d+|ntp_socket=-?\d+)"
)
TIME_TRACE = re.compile(
    rb"CRABIT:RADIO t=\d+ sid=\d+ ev=TIME_(?:RX|QUEUED|HANDLED|REJECTED|APPLIED|READBACK|RESULT) "
    rb"a=\d+ b=\d+ lost=\d+"
)
LED_TRACE = re.compile(
    rb"\[LED\] (?:BEGIN|hw=\d+ start=\d+ stop=\d+ bright=\d+ grb=\d+ "
    rb"cr1=\d+ ccer=\d+ ccmr1=\d+ psc=\d+ arr=\d+ cnta=\d+ cntb=\d+ clk=\d+ "
    rb"mode=\d+ af=\d+ type=\d+)"
)


def safe_records(line, network=False, all_radio=False, led=False):
    if led:
        match = LED_TRACE.fullmatch(line.rstrip(b"\r"))
        if match:
            return [match.group()]
    if network:
        # Whole-line matching rejects appended arbitrary text and credentials.
        stripped = line.rstrip(b"\r")
        radio = TRACE if all_radio else TIME_TRACE
        match = NETWORK_TRACE.fullmatch(stripped) or radio.fullmatch(stripped)
        return [match.group()] if match else []
    return [match.group() for match in TRACE.finditer(line)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--port")
    parser.add_argument("--seconds", type=float, default=90)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--led", action="store_true", help="also retain fixed numeric LED diagnostics"
    )
    parser.add_argument(
        "--reconnect",
        action="store_true",
        help="retry only the explicit port within the original duration",
    )
    parser.add_argument(
        "--network",
        action="store_true",
        help="retain only structured weather/RTC diagnostics, not arbitrary logs",
    )
    parser.add_argument(
        "--all-radio",
        action="store_true",
        help="with --network, also retain all structured radio trace events",
    )
    args = parser.parse_args()
    import serial
    from serial.tools import list_ports

    if args.list:
        for item in list_ports.comports():
            print(f"{item.device}: {item.description} vid={item.vid} pid={item.pid}")
        return
    if not args.port or not 0 < args.seconds <= 600:
        parser.error("explicit --port and duration in (0, 600] are required")
    root = Path(__file__).resolve().parents[1]
    output = args.output or root / "build/companion-traces" / (
        datetime.now().strftime("%Y%m%d-%H%M%S") + ".log"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    connection = None
    waiting = False
    buffer = bytearray()
    count = 0
    try:
        with output.open("x", encoding="ascii") as log:
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                try:
                    if connection is None:
                        # Set controls before every open; never toggle afterward.
                        connection = serial.Serial(
                            port=None, baudrate=115200, timeout=0.2, rtscts=False, dsrdtr=False
                        )
                        connection.dtr = False
                        connection.rts = False
                        connection.port = args.port
                        connection.open()
                        print(f"LISTENING {args.port}; read-only; {output}", flush=True)
                        waiting = False
                    buffer.extend(connection.read(512))
                except (serial.SerialException, OSError):
                    if connection is not None:
                        try:
                            connection.close()
                        except (serial.SerialException, OSError):
                            pass
                        connection = None
                    buffer.clear()  # Never splice lines across USB enumerations.
                    if not args.reconnect:
                        raise
                    if not waiting:
                        print(f"WAITING {args.port}; disconnected; passive retry only", flush=True)
                        waiting = True
                    time.sleep(0.2)
                    continue
                while b"\n" in buffer:
                    line, _, rest = buffer.partition(b"\n")
                    buffer = bytearray(rest)
                    # Ignore all other logs rather than retaining arbitrary data.
                    for record in safe_records(line, args.network, args.all_radio, args.led):
                        safe = record.decode("ascii")
                        log.write(safe + "\n")
                        log.flush()
                        print(safe, flush=True)
                        count += 1
                if len(buffer) > 4096:
                    buffer.clear()
    finally:
        if connection is not None:
            connection.close()
    print(f"CAPTURED {count} records; {output}", flush=True)


if __name__ == "__main__":
    main()
