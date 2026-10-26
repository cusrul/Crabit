"""No hardware needed: verify passive capture and sanitized persisted output."""

import contextlib
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from tools import capture_companion_trace as capture


class CaptureTest(unittest.TestCase):
    def test_led_capture_is_explicit_and_rejects_arbitrary_suffixes(self):
        records = (
            b"[LED] BEGIN",
            b"[LED] hw=1 start=0 stop=0 bright=100 grb=65280 "
            b"cr1=1 ccer=1 ccmr1=104 psc=0 arr=99 cnta=10 cntb=55 clk=1 mode=2 af=1 type=0",
        )
        for record in records:
            with self.subTest(record=record):
                self.assertEqual(capture.safe_records(record, True, True, True), [record])
                self.assertEqual(capture.safe_records(record, True, True), [])
                self.assertEqual(capture.safe_records(record + b" PRIVATE", True, True, True), [])
        self.assertEqual(capture.safe_records(b"[LED] PRIVATE", True, True, True), [])

    def test_ui_mode_numeric_trace_records_survive_full_radio_capture(self):
        events = (
            "UI_ENTRY",
            "UI_STAGE",
            "UI_HEARTBEAT",
            "MODE_QUEUED",
            "MODE_TX_BEGIN",
            "MODE_TX_END",
            "BLE_STOP_REQUESTED",
            "BLE_OFF_COMPLETE",
            "BLE_RESUME_QUEUED",
            "BW_UI_INIT_BEGIN",
            "BW_UI_OLED_READY",
            "BW_UI_LAUNCHER_READY",
        )
        for index, event in enumerate(events):
            with self.subTest(event=event):
                line = (
                    f"CRABIT:RADIO t={200 + index} sid=3 ev={event} "
                    f"a={10 + index} b={20 + index} lost=0"
                ).encode("ascii")
                self.assertEqual(capture.safe_records(line, True, True), [line])
                self.assertEqual(capture.safe_records(line, True), [])
                self.assertEqual(capture.safe_records(line + b" PRIVATE_TOKEN", True, True), [])

    def test_combined_network_and_full_radio_capture_remains_whitelisted(self):
        radio = b"CRABIT:RADIO t=10 sid=0 ev=UART_ERROR a=4 b=6422720 lost=983"
        network = b"[NET] id=2 stage=TIME_OK"
        self.assertEqual(capture.safe_records(radio, True, True), [radio])
        self.assertEqual(capture.safe_records(network, True, True), [network])
        self.assertEqual(capture.safe_records(radio, True), [])
        self.assertEqual(capture.safe_records(radio + b" PRIVATE", True, True), [])
        self.assertEqual(capture.safe_records(b"password=PRIVATE", True, True), [])

    def test_network_capture_accepts_only_structured_diagnostics(self):
        for line in (
            b"[NET] id=1 stage=CFG_OK:ssid",  # lowercase fields are deliberately not retained
            b"[NET] id=1 password=secret",
            b"[NET] id=1 http=200 PRIVATE_TOKEN",
            b"noise [NET] id=1 http=200",
        ):
            self.assertEqual(capture.safe_records(line, True), [])
        for line in (
            b"[NET] id=1 stage=TIME_OK",
            b"[NET] id=1 progress=HTTP_BODY rtc=1",
            b"[NET] id=1 result=TIMEOUT last=HTTP_READ rtc=0 days=0",
            b"[NET] id=1 rtc_time=0 rtc_date=0 local=2026-10-20 12:01:02",
            b"[NET] id=1 rtc_read=0,0 value=2026-10-20 12:01:02",
            b"[NET] id=1 bytes=512 ms=12001 ok=0",
            b"[NET] id=1 http=429",
            b"[NET] id=1 ntp=3 reason=1 epoch=0",
            b"[NET] id=1 ntp_socket=0",
            b"[NET] id=1 ntp_socket=-1",
            b"CRABIT:RADIO t=100 sid=1 ev=TIME_RX a=10 b=1234 lost=0",
            b"CRABIT:RADIO t=101 sid=1 ev=TIME_QUEUED a=1 b=0 lost=0",
            b"CRABIT:RADIO t=102 sid=1 ev=TIME_HANDLED a=5 b=3 lost=0",
            b"CRABIT:RADIO t=103 sid=1 ev=TIME_REJECTED a=4 b=257 lost=0",
            b"CRABIT:RADIO t=104 sid=1 ev=TIME_READBACK a=20261020 b=120001 lost=0",
            b"CRABIT:RADIO t=104 sid=1 ev=TIME_APPLIED a=1790000000 b=120001 lost=0",
            b"CRABIT:RADIO t=105 sid=1 ev=TIME_RESULT a=1 b=7 lost=0",
        ):
            self.assertEqual(capture.safe_records(line + b"\r", True), [line])

    def test_reconnect_same_port_without_writes_and_without_splicing_old_line(self):
        class SerialError(OSError):
            pass

        instances = []

        class FakeSerial:
            def __init__(self, **kwargs):
                assert kwargs["port"] is None
                self.index = len(instances)
                self.reads = 0
                self.closed = False
                instances.append(self)

            def open(self):
                assert self.port == "TEST"
                assert self.dtr is False and self.rts is False
                if self.index == 0:
                    raise SerialError("absent")

            def read(self, _):
                self.reads += 1
                if self.index == 1:
                    if self.reads == 1:
                        return b"CRABIT:RADIO t=1 sid=1 ev=START a=0 b=0"
                    raise SerialError("USB removed")
                if self.reads == 1:
                    return b" lost=0\nCRABIT:RADIO t=2 sid=2 ev=START a=0 b=0 lost=0\n"
                return b""

            def write(self, _):
                raise AssertionError("no device commands")

            def close(self):
                self.closed = True

        ticks = iter(i / 10 for i in range(30))
        fake = SimpleNamespace(Serial=FakeSerial, SerialException=SerialError)
        with tempfile.TemporaryDirectory(prefix="crabit-reconnect-") as folder:
            output = Path(folder) / "trace.log"
            with (
                patch.dict(
                    sys.modules, {"serial": fake, "serial.tools": SimpleNamespace(list_ports=None)}
                ),
                patch.object(
                    sys,
                    "argv",
                    [
                        "capture",
                        "--port",
                        "TEST",
                        "--seconds",
                        "1",
                        "--reconnect",
                        "--output",
                        str(output),
                    ],
                ),
                patch.object(capture.time, "monotonic", side_effect=lambda: next(ticks)),
                patch.object(capture.time, "sleep"),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                capture.main()
            self.assertEqual(len(instances), 3)
            self.assertTrue(all(item.closed for item in instances))
            self.assertEqual(output.read_text(), "CRABIT:RADIO t=2 sid=2 ev=START a=0 b=0 lost=0\n")

    def test_capture_never_writes_device_and_sets_modem_controls_before_open(self):
        class FakeSerial:
            def __init__(self, **kwargs):
                assert kwargs["port"] is None
                self.closed = False

            def open(self):
                assert self.dtr is False and self.rts is False
                assert self.port == "TEST"

            def write(self, _):
                raise AssertionError("capture must never write to device")

            def read(self, _):
                return (
                    b"unrelated PRIVATE_TOKEN\n"
                    b"CRABIT:RADIO t=1 sid=2 ev=START a=0 b=0 lost=0\r\n"
                )

            def close(self):
                self.closed = True

        connection = FakeSerial(port=None)
        fake = SimpleNamespace(Serial=lambda **kwargs: connection)
        with tempfile.TemporaryDirectory(prefix="crabit-capture-") as folder:
            output = Path(folder) / "trace.log"
            with (
                patch.dict(
                    sys.modules, {"serial": fake, "serial.tools": SimpleNamespace(list_ports=None)}
                ),
                patch.object(
                    sys,
                    "argv",
                    ["capture", "--port", "TEST", "--seconds", "1", "--output", str(output)],
                ),
                patch.object(capture.time, "monotonic", side_effect=[0.0, 0.1, 2.0]),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                capture.main()
            self.assertTrue(connection.closed)
            self.assertEqual(output.read_text(), "CRABIT:RADIO t=1 sid=2 ev=START a=0 b=0 lost=0\n")


if __name__ == "__main__":
    unittest.main()
