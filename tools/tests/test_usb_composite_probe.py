import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).parents[1] / "probe_usb_composite.py"
SPEC = importlib.util.spec_from_file_location("usb_composite_probe", MODULE_PATH)
probe_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe_module)


class FakeSerial:
    def __init__(self, replies=(), *, stall=False):
        self.replies = bytearray(b"".join(replies))
        self.writes = []
        self.timeout = 0
        self.stall = stall
        self.closed = False
        self.typed = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.closed = True

    def write(self, data):
        self.writes.append(data)
        nonce = data.decode().split()[-1]
        if "STATUS" in data.decode():
            state = "DONE" if self.typed else "IDLE"
            self.replies.extend(f"CRABIT:USB:STATUS {nonce} cdc=1 hid=1 test={state}\n".encode())
        elif "ECHO" in data.decode():
            self.replies.extend(f"CRABIT:USB:ECHO {nonce}\n".encode())
        elif "TYPE" in data.decode():
            self.typed = True
            self.replies.extend(f"CRABIT:USB:TYPE {nonce} QUEUED\n".encode())
        return len(data)

    def read(self, size):
        if self.stall:
            return b""
        count = min(size, len(self.replies))
        result = bytes(self.replies[:count])
        del self.replies[:count]
        return result


class USBProbeTests(unittest.TestCase):
    def make_probe(self, fake, nonces=None):
        values = iter(nonces or [f"{n:08X}" for n in range(1, 100)])
        return probe_module.USBCompositeProbe(fake, nonce_factory=lambda: next(values))

    def test_status_and_twenty_echoes_are_default_and_nonce_varies(self):
        fake = FakeSerial()
        made = []

        def factory(**kwargs):
            made.append(kwargs)
            return fake

        probe_module.run("COM9", serial_factory=factory, output=lambda *_: None)
        self.assertEqual(len(fake.writes), 21)
        self.assertTrue(all(len(w) <= 64 and w.endswith(b"\n") for w in fake.writes))
        self.assertEqual(len({w.split()[-1] for w in fake.writes}), 21)
        self.assertEqual(made[0]["port"], "COM9")

    def test_unrelated_logs_and_stale_nonce_filtered(self):
        fake = FakeSerial([b"USBLOG booted\n", b"CRABIT:USB:ECHO 00000000\n"])
        self.make_probe(fake, ["1234ABCD"]).echo()
        self.assertEqual(fake.writes, [b"CRABIT:USB:ECHO 1234ABCD\n"])

    def test_timeout(self):
        fake = FakeSerial(stall=True)
        probe = self.make_probe(fake, ["00000001"])
        with self.assertRaisesRegex(probe_module.ProbeError, "timed out"):
            probe.echo()

    def test_overlong_receive_buffer_rejected(self):
        fake = FakeSerial([b"x" * (probe_module.MAX_BUFFER + 1)])
        probe = self.make_probe(fake, ["00000001"])
        with self.assertRaisesRegex(probe_module.ProbeError, "buffer exceeded"):
            probe.echo()

    def test_type_gates_before_port_open(self):
        opened = []
        with self.assertRaisesRegex(probe_module.ProbeError, "requires --confirm-target"):
            probe_module.run("COM1", do_type=True, serial_factory=lambda **_: opened.append(True))
        self.assertEqual(opened, [])

    def test_partial_write_stops_without_replay(self):
        class Partial(FakeSerial):
            def write(self, data):
                self.writes.append(data)
                return len(data) - 1

        fake = Partial()
        with self.assertRaisesRegex(probe_module.ProbeError, "incomplete serial write"):
            self.make_probe(fake).echo()
        self.assertEqual(len(fake.writes), 1)

    def test_device_overflow_is_reported_without_retry(self):
        fake = FakeSerial([b"CRABIT:USB:ERROR OVERFLOW\n"])
        with self.assertRaisesRegex(probe_module.ProbeError, "ERROR OVERFLOW"):
            self.make_probe(fake).echo()
        self.assertEqual(len(fake.writes), 1)

    def test_continuous_unrelated_lines_cannot_extend_deadline(self):
        fake = FakeSerial([b"log\n" * 2000])
        ticks = iter(n / 10 for n in range(100))
        probe = probe_module.USBCompositeProbe(
            fake, nonce_factory=lambda: "1234ABCD", clock=lambda: next(ticks)
        )
        with self.assertRaisesRegex(probe_module.ProbeError, "timed out"):
            probe.echo()
        self.assertEqual(len(fake.writes), 1)

    def test_type_sends_only_fixed_text_flow(self):
        fake = FakeSerial()
        outputs = []
        probe_module.run(
            "COM1",
            do_type=True,
            confirm_target=True,
            serial_factory=lambda **_: fake,
            output=outputs.append,
        )
        commands = [w.decode().rstrip("\n") for w in fake.writes]
        self.assertEqual(sum(c.startswith("CRABIT:USB:TYPE ") for c in commands), 1)
        self.assertEqual(sum(c.startswith("CRABIT:USB:ECHO ") for c in commands), 40)
        self.assertTrue(any("crabit usb test\\n" in line for line in outputs))
        self.assertTrue(any("requires UI observation" in line for line in outputs))
        self.assertFalse(any("crabit usb test" in c for c in commands))


if __name__ == "__main__":
    unittest.main()
