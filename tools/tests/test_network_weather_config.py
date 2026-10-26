import sys
import contextlib
import io
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1]
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import configure_network_weather as config


class CredentialValidationTests(unittest.TestCase):
    def test_ssid_uses_utf8_byte_limit(self):
        self.assertEqual(len(config.validate_ssid("a" * 32)), 32)
        with self.assertRaises(config.ConfigurationError):
            config.validate_ssid("a" * 33)
        with self.assertRaises(config.ConfigurationError):
            config.validate_ssid("界" * 11)  # 33 UTF-8 bytes, not 11 characters.

    def test_password_uses_utf8_byte_limit_and_allows_open_network(self):
        self.assertEqual(config.validate_password(""), b"")
        self.assertEqual(len(config.validate_password("p" * 64)), 64)
        self.assertEqual(len(config.validate_password("界" * 21)), 63)
        with self.assertRaises(config.ConfigurationError):
            config.validate_password("p" * 65)
        with self.assertRaises(config.ConfigurationError):
            config.validate_password("界" * 22)  # 66 UTF-8 bytes.

    def test_credential_values_are_hex_of_utf8_bytes(self):
        self.assertEqual(config.encode_credential_setting("ssid", "网络"), "ssid=e7bd91e7bb9c")
        self.assertEqual(config.encode_credential_setting("pass", "päss"), "pass=70c3a47373")

    def test_auth_code_must_be_six_ascii_digits(self):
        config.validate_auth_code("012345")
        for invalid in ("12345", "1234567", "12a456", "１２３４５６"):
            with self.subTest(invalid=invalid), self.assertRaises(config.ConfigurationError):
                config.validate_auth_code(invalid)

    def test_command_line_password_is_rejected_without_echo(self):
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            result = config.main(
                [
                    "--password=do-not-echo",
                    "--latitude",
                    "0",
                    "--longitude",
                    "0",
                ]
            )
        self.assertEqual(result, 2)
        self.assertNotIn("do-not-echo", output.getvalue())


class LocationAndTimeValidationTests(unittest.TestCase):
    def test_coordinate_boundaries_and_microdegree_rounding(self):
        self.assertEqual(config.coordinate_to_e6("90", "Latitude", -90, 90), 90_000_000)
        self.assertEqual(config.coordinate_to_e6("-180", "Longitude", -180, 180), -180_000_000)
        self.assertEqual(config.coordinate_to_e6("31.2304015", "Latitude", -90, 90), 31_230_402)

    def test_rejects_out_of_range_and_nonfinite_coordinates(self):
        for value in ("90.000001", "-90.000001", "NaN", "Infinity", "-Infinity"):
            with self.subTest(latitude=value), self.assertRaises(config.ConfigurationError):
                config.coordinate_to_e6(value, "Latitude", -90, 90)
        for value in ("180.000001", "-180.000001", "nan"):
            with self.subTest(longitude=value), self.assertRaises(config.ConfigurationError):
                config.coordinate_to_e6(value, "Longitude", -180, 180)

    def test_place_setting_and_timezone_forecast_ranges(self):
        self.assertEqual(
            config.build_place_setting("31.2304", "121.4737", 480, 3),
            "place=31230400,121473700,480,3",
        )
        config.build_place_setting("0", "0", -720, 1)
        config.build_place_setting("0", "0", 840, 7)
        for tz in (-721, 841):
            with self.subTest(tz=tz), self.assertRaises(config.ConfigurationError):
                config.build_place_setting("0", "0", tz, 3)
        for days in (0, 8, True):
            with self.subTest(days=days), self.assertRaises(config.ConfigurationError):
                config.build_place_setting("0", "0", 480, days)


class ProtocolEncodingTests(unittest.TestCase):
    def test_request_is_compact_and_correlated(self):
        payload = config.make_network_request(23, "ssid=6869")
        self.assertEqual(payload, b'{"i":23,"t":"app/net","v":"ssid=6869"}')
        self.assertEqual(
            config.make_network_request(24, "read"), b'{"i":24,"t":"app/net","v":"read"}'
        )
        self.assertTrue(config.is_correlated_response({"i": 23, "t": "app/net", "v": "OK"}, 23))
        self.assertFalse(config.is_correlated_response({"i": 23, "t": "sys/ble", "v": "READY"}, 23))
        self.assertFalse(config.is_correlated_response({"i": "23", "t": "app/net", "v": "OK"}, 23))
        self.assertFalse(config.is_correlated_response({"i": True, "t": "app/net", "v": "OK"}, 1))

    def test_payload_chunks_are_at_most_twenty_bytes(self):
        payload = b"0123456789" * 9
        chunks = config.chunk_payload(payload)
        self.assertEqual(b"".join(chunks), payload)
        self.assertTrue(chunks)
        self.assertLessEqual(max(map(len, chunks)), 20)
        self.assertEqual(config.chunk_payload(b""), [])

    def test_current_firmware_auth_ack_is_recognized(self):
        self.assertTrue(config.is_auth_ack({"t": "auth", "v": "1"}))
        self.assertFalse(config.is_auth_ack({"t": "auth", "v": "AUTH_OK"}))
        self.assertFalse(config.is_auth_ack({"t": "sys/ble", "v": "READY"}))

    def test_busy_ack_has_a_safe_error_and_readback_is_nonsecret(self):
        with self.assertRaisesRegex(config.ConfigurationError, "reported BLE/network busy"):
            config.validate_config_ack("RADIO_BUSY")
        with self.assertRaisesRegex(config.ConfigurationError, "reported BLE/network busy"):
            config.raise_if_busy("BUSY")
        with self.assertRaises(config.ConfigurationError):
            config.validate_config_ack("ssid=secret-value")
        self.assertEqual(config.parse_readback("configured=1;days=3", 3), 3)
        with self.assertRaises(config.ConfigurationError):
            config.parse_readback("configured=1;days=4", 3)
        with self.assertRaises(config.ConfigurationError):
            config.parse_readback("configured=0;days=3", 3)

    def test_request_ids_wrap_without_zero(self):
        ids = config.RequestIds(0xFFFF)
        self.assertEqual([ids.next(), ids.next(), ids.next()], [0xFFFF, 1, 2])


class NotificationStreamTests(unittest.TestCase):
    def test_partial_utf8_and_multiple_json_objects(self):
        stream = config.JsonObjectStream()
        wire = (
            '{"t":"sys/ble","v":"天气就绪"}'
            '{"t":"sys/notify","v":"brace { inside a string }"}'
            '{"i":27,"t":"app/net","v":"OK"}'
        ).encode("utf-8")
        split = wire.index("天气".encode("utf-8")) + 1  # Split inside a multibyte UTF-8 character.
        self.assertEqual(stream.feed(wire[:split]), [])
        self.assertEqual(
            stream.feed(wire[split:]),
            [
                {"t": "sys/ble", "v": "天气就绪"},
                {"t": "sys/notify", "v": "brace { inside a string }"},
                {"i": 27, "t": "app/net", "v": "OK"},
            ],
        )

    def test_stream_waits_for_partial_json_and_rejects_oversize(self):
        stream = config.JsonObjectStream(maximum_buffer_bytes=32)
        self.assertEqual(stream.feed(b'{"t":"app/'), [])
        self.assertEqual(stream.feed(b'net","v":"OK"}'), [{"t": "app/net", "v": "OK"}])
        with self.assertRaises(config.ConfigurationError):
            stream.feed(b'{"v":"' + b"x" * 40)

    def test_stream_rejects_malformed_utf8_and_never_emits_invalid_json(self):
        with self.assertRaises(config.ConfigurationError):
            config.JsonObjectStream().feed(b'{"v":"\xff"}')
        stream = config.JsonObjectStream()
        self.assertEqual(stream.feed(b'{"v":xx}'), [])
        self.assertEqual(stream.feed(b"{}"), [])


if __name__ == "__main__":
    unittest.main()
