"""Uploader regression tests: fake serial only; never open a device."""

import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

SPEC = importlib.util.spec_from_file_location(
    "flash_bw16", Path(__file__).parents[1] / "flash_bw16.py"
)
flash = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(flash)


class FlashTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.main = self.root / "main.bin"
        self.ext = self.root / "extension.bin"
        self.loader = self.root / "loader.bin"
        self.report = self.root / "report.json"
        self.main.write_bytes(b"main" * 256)
        self.ext.write_bytes(b"ext!" * 4)
        self.loader.write_bytes(b"loader")
        self.evidence = {
            "status": "PASS",
            "flashable": False,
            "hardware": "NOT TESTED",
            "main_bytes": self.main.stat().st_size,
            "extension_bytes": self.ext.stat().st_size,
            "main_sha256": flash.digest(self.main.read_bytes()),
            "extension_sha256": flash.digest(self.ext.read_bytes()),
            "main_physical_end": hex(0x6000 + self.main.stat().st_size),
            "extension_offset": "0x107000",
            "extension_end": hex(0x107000 + self.ext.stat().st_size),
        }
        self.save_report()

    def save_report(self):
        self.report.write_text(json.dumps(self.evidence), encoding="utf-8")

    def pair(self):
        return flash.ImagePair(self.main, self.ext, self.report, self.loader)

    def protocol(self):
        port = Mock()
        port.write.side_effect = len
        with patch.object(
            flash, "serial", SimpleNamespace(Serial=Mock(return_value=port), PARITY_NONE="N")
        ):
            protocol = flash.Protocol("FAKE")
        return protocol, port

    def test_pair_accepts_build_evidence_not_hardware_approval(self):
        pair = self.pair()
        self.assertEqual(pair.main, self.main.read_bytes())

    def test_pair_rejects_wrong_hash_size_address_and_failed_build(self):
        original = dict(self.evidence)
        for key, value in (
            ("main_sha256", "wrong"),
            ("extension_sha256", "wrong"),
            ("main_bytes", 0),
            ("extension_bytes", 0),
            ("extension_offset", "0x106000"),
            ("extension_end", "0x200000"),
            ("main_physical_end", "0xFC000"),
            ("status", "FAIL"),
        ):
            with self.subTest(key=key):
                self.evidence = {**original, key: value}
                self.save_report()
                with self.assertRaises(ValueError):
                    self.pair()

    def test_pair_rejects_same_file(self):
        with self.assertRaises(ValueError):
            flash.ImagePair(self.main, self.main, self.report, self.loader)

    def test_pair_rejects_empty_flashloader(self):
        self.loader.write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "Empty flashloader"):
            self.pair()

    def test_pair_rejects_missing_or_nonnumeric_report_address(self):
        original = dict(self.evidence)
        for key, value in (("extension_offset", None), ("extension_end", "not-a-number")):
            with self.subTest(key=key):
                self.evidence = {**original, key: value}
                self.save_report()
                with self.assertRaisesRegex(ValueError, f"Build report mismatch: {key}"):
                    self.pair()

    def test_pair_rejects_empty_unaligned_and_oversized_images(self):
        for path, size in (
            (self.main, 0),
            (self.main, 3),
            (self.main, flash.MAIN_MAX + 4),
            (self.ext, 0),
            (self.ext, 7),
            (self.ext, flash.EXT_END - flash.EXT_ADDR + 4),
        ):
            with self.subTest(path=path, size=size):
                old = path.read_bytes()
                path.write_bytes(b"x" * size)
                with self.assertRaises(ValueError):
                    self.pair()
                path.write_bytes(old)

    def test_snapshot_not_reread_after_validation(self):
        pair = self.pair()
        self.main.write_bytes(b"changed")
        self.assertEqual(pair.main, b"main" * 256)

    def test_packet_matches_prototype_and_sequence_wraps(self):
        protocol, port = self.protocol()
        protocol.wait_sync = Mock(return_value=True)
        protocol.wait_ack = Mock(return_value=True)
        seq = protocol.write_block(flash.EXT_ADDR, b"abcd", 255, "extension")
        packet = port.write.call_args.args[0]
        expected = bytearray(
            b"\x02\xff\x00" + struct.pack("<I", flash.EXT_ADDR) + b"abcd" + b"\xff" * 1020
        )
        expected.append((0xFF + sum(expected)) & 0xFF)
        self.assertEqual(packet, expected)
        self.assertEqual(len(packet), 1032)
        self.assertEqual(seq, 0)

    def test_multichunk_packets_advance_address_and_pad_last_chunk(self):
        protocol, port = self.protocol()
        protocol.wait_sync = Mock(return_value=True)
        protocol.wait_ack = Mock(return_value=True)
        data = b"\x11" * 1024 + b"tail"
        seq = protocol.write_block(flash.MAIN_ADDR, data, 0xFE, "main image")
        self.assertEqual(port.write.call_count, 2)
        first, second = (call.args[0] for call in port.write.call_args_list)
        self.assertEqual(first[1:3], bytes([0xFE, 0x01]))
        self.assertEqual(second[1:3], bytes([0xFF, 0x00]))
        self.assertEqual(struct.unpack_from("<I", second, 3)[0], flash.MAIN_ADDR + 1024)
        self.assertEqual(first[7:1031], data[:1024])
        self.assertEqual(second[7:1031], b"tail" + b"\xff" * 1020)
        for packet in (first, second):
            self.assertEqual(packet[1031], (0xFF + sum(packet[:1031])) & 0xFF)
        self.assertEqual(seq, 0)

    def test_erase_command_matches_prototype_at_both_regions(self):
        protocol, _ = self.protocol()
        protocol.send = Mock()
        for address, size in (
            (flash.MAIN_ADDR, 999424),
            (flash.EXT_ADDR, 91520),
            (flash.OTA2_ADDR, 4096),
        ):
            protocol.erase_block(address, size, "test")
            legacy = bytearray(6)
            legacy[0] = 0x17
            struct.pack_into("<I", legacy, 1, address)
            sectors = (size + 4095) // 4096
            legacy[4], legacy[5] = sectors & 255, sectors >> 8
            self.assertEqual(protocol.send.call_args.args[0], legacy)
            self.assertEqual(protocol.send.call_args.kwargs["ack_timeout"], 120)

    def test_verify_command_matches_prototype(self):
        protocol, port = self.protocol()
        protocol.wait_sync = Mock(return_value=True)
        data = b"abcd" * 4
        response = b"\x27" + struct.pack("<I", flash.word_checksum(data))
        protocol.read_byte = Mock(side_effect=response)
        protocol.verify(flash.EXT_ADDR, data, "extension")
        legacy = bytearray(8)
        legacy[0] = 0x27
        struct.pack_into("<I", legacy, 1, flash.EXT_ADDR & ~0x8000000)
        struct.pack_into("<I", legacy, 4, len(data))
        self.assertEqual(port.write.call_args.args[0], legacy[:7])

    def test_verify_checksums_with_sync_value_in_payload(self):
        protocol, _ = self.protocol()
        protocol.wait_sync = Mock(return_value=True)
        protocol.read_byte = Mock(side_effect=[0x15, 0x27, 0x15, 0, 0, 0])
        protocol.verify(flash.MAIN_ADDR, b"\x15\x00\x00\x00", "test")

    def test_bad_checksum_fails(self):
        protocol, _ = self.protocol()
        protocol.wait_sync = Mock(return_value=True)
        protocol.read_byte = Mock(side_effect=[0x27, 0, 0, 0, 0])
        with self.assertRaisesRegex(flash.FlashError, "Checksum mismatch"):
            protocol.verify(flash.MAIN_ADDR, b"abcd", "test")

    def test_unexpected_ack_and_verify_bytes_fail_fast(self):
        protocol, _ = self.protocol()
        protocol.read_byte = Mock(return_value=0x33)
        with self.assertRaisesRegex(flash.FlashError, "Unexpected response 0x33"):
            protocol.wait_ack()
        protocol.wait_sync = Mock(return_value=True)
        with self.assertRaisesRegex(flash.FlashError, "Unexpected verify response 0x33"):
            protocol.verify(flash.MAIN_ADDR, b"abcd", "test")

    def test_close_failure_does_not_raise_and_keeps_diagnosis(self):
        protocol, port = self.protocol()
        port.close.side_effect = OSError("port already gone")
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            protocol.close()
        self.assertIn("serial close failed", output.getvalue())

    def test_sync_ack_and_verify_timeouts_use_total_deadline(self):
        for kind in ("sync", "ack", "verify"):
            protocol, _ = self.protocol()
            protocol.read_byte = Mock(return_value=-1)
            if kind == "verify":
                protocol.wait_sync = Mock(return_value=True)
            # A fake monotonic clock advances one second per call, no real wait.
            ticks = iter(range(100))
            with patch.object(flash.time, "monotonic", side_effect=lambda: next(ticks)):
                try:
                    if kind == "verify":
                        result = protocol.verify(flash.MAIN_ADDR, b"abcd", "test")
                    else:
                        result = protocol.wait_sync() if kind == "sync" else protocol.wait_ack()
                    self.assertFalse(result)
                except flash.FlashError as error:
                    self.assertIn("timeout", str(error).lower())
            self.assertLessEqual(protocol.read_byte.call_count, 20)

    def test_short_write_fails_with_address(self):
        protocol, port = self.protocol()
        port.write.side_effect = None
        port.write.return_value = 1
        protocol.wait_sync = Mock(return_value=True)
        with self.assertRaisesRegex(flash.FlashError, "chunk 1/1 at 0x08107000"):
            protocol.write_block(flash.EXT_ADDR, b"abcd", 1, "extension")

    def test_execute_preserves_erase_write_verify_order_and_wait(self):
        protocol = Mock()
        protocol.wait_sync.return_value = True
        protocol.write_block.return_value = 9
        pair = self.pair()
        with (
            patch.object(flash, "Protocol", return_value=protocol),
            patch.object(flash.time, "sleep") as sleep,
        ):
            self.assertEqual(flash.execute(pair, "FAKE", 3), 0)
        sleep.assert_called_once_with(3)
        erase = [call.args[0] for call in protocol.erase_block.call_args_list]
        self.assertEqual(erase, [flash.OTA2_ADDR, flash.MAIN_ADDR, flash.EXT_ADDR])
        writes = [call.args for call in protocol.write_block.call_args_list]
        self.assertEqual(
            [call[0] for call in writes], [flash.FLASHLOADER_ADDR, flash.MAIN_ADDR, flash.EXT_ADDR]
        )
        self.assertEqual(writes[2][2], 9)
        self.assertEqual(
            [call.args[0] for call in protocol.verify.call_args_list],
            [flash.MAIN_ADDR, flash.EXT_ADDR],
        )
        # Verify must hash the pre-validated snapshot, not a re-read of possibly replaced files.
        for call in protocol.verify.call_args_list:
            self.assertIn(call.args[1], (pair.main, pair.extension))
        self.assertEqual(writes[2][1], pair.extension)
        protocol.close.assert_called_once()

    def test_interrupt_during_wait_reports_wait_phase_and_erases_nothing(self):
        protocol = Mock()
        protocol.wait_sync.return_value = True
        output = io.StringIO()
        with (
            patch.object(flash, "Protocol", return_value=protocol),
            patch.object(flash.time, "sleep", side_effect=KeyboardInterrupt),
            contextlib.redirect_stderr(output),
        ):
            self.assertEqual(flash.execute(self.pair(), "FAKE", 5), 1)
        text = output.getvalue()
        self.assertIn("before first sync", text)
        self.assertIn("No flash erase command was issued", text)
        protocol.erase_block.assert_not_called()
        protocol.close.assert_called_once()

    def test_failed_erase_ack_warns_about_possible_damage(self):
        protocol = Mock()
        protocol.wait_sync.return_value = True
        protocol.erase_block.side_effect = OSError("disconnect")
        output = io.StringIO()
        with (
            patch.object(flash, "Protocol", return_value=protocol),
            contextlib.redirect_stderr(output),
        ):
            self.assertEqual(flash.execute(self.pair(), "FAKE", 0), 1)
        self.assertIn("may be incomplete", output.getvalue())
        self.assertIn("SAME verified pair", output.getvalue())
        protocol.close.assert_called_once()

    def test_missing_sync_does_not_erase(self):
        protocol = Mock()
        protocol.wait_sync.return_value = False
        with (
            patch.object(flash, "Protocol", return_value=protocol),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(flash.execute(self.pair(), "FAKE", 0), 1)
        protocol.erase_block.assert_not_called()

    def test_dry_run_never_opens_serial(self):
        with patch.object(flash, "Protocol") as protocol:
            self.assertEqual(
                flash.main(
                    [
                        "--main",
                        str(self.main),
                        "--extension",
                        str(self.ext),
                        "--report",
                        str(self.report),
                        "--flashloader",
                        str(self.loader),
                    ]
                ),
                0,
            )
        protocol.assert_not_called()

    def test_invalid_pair_rejected_before_opening_serial(self):
        self.ext.write_bytes(b"wrong!!!")
        with patch.object(flash, "Protocol") as protocol, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(
                flash.main(
                    [
                        "--main",
                        str(self.main),
                        "--extension",
                        str(self.ext),
                        "--report",
                        str(self.report),
                        "--flashloader",
                        str(self.loader),
                        "--execute",
                        "--retire-ota2",
                        "--port",
                        "FAKE",
                    ]
                ),
                1,
            )
        protocol.assert_not_called()

    def test_probe_is_listen_only(self):
        protocol = Mock()
        protocol.read_byte.return_value = flash.SYNC
        ticks = iter(range(20))
        with (
            patch.object(flash, "Protocol", return_value=protocol),
            patch.object(flash.time, "monotonic", side_effect=lambda: next(ticks)),
        ):
            self.assertEqual(flash.probe("FAKE", 3), 0)
        protocol.write.assert_not_called()
        protocol.send.assert_not_called()
        protocol.close.assert_called_once()

    def test_probe_without_sync_reports_recovery_hint(self):
        protocol = Mock()
        protocol.read_byte.return_value = -1
        ticks = iter(range(20))
        output = io.StringIO()
        with (
            patch.object(flash, "Protocol", return_value=protocol),
            patch.object(flash.time, "monotonic", side_effect=lambda: next(ticks)),
            contextlib.redirect_stderr(output),
        ):
            self.assertEqual(flash.probe("FAKE", 3), 1)
        self.assertIn("No sync seen", output.getvalue())
        protocol.write.assert_not_called()

    def test_cli_rejects_conflicting_modes_and_missing_authorization(self):
        for args in (["--probe", "--execute"], ["--execute", "--port", "FAKE"], ["--probe"]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                flash.main(args)

    def test_seconds_must_be_finite_and_nonnegative(self):
        for value in ("-1", "nan", "inf"):
            with self.assertRaises(argparse.ArgumentTypeError):
                flash.nonnegative_seconds(value)

    def test_loader_path_uses_local_appdata(self):
        with patch.dict(flash.os.environ, {"LOCALAPPDATA": str(self.root)}):
            self.assertTrue(flash.default_flashloader().is_relative_to(self.root))


if __name__ == "__main__":
    unittest.main()
