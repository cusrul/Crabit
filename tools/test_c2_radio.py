"""Host checks for the bounded C2 RadioSvc contract.

This mirrors the ownership rules without importing the Ameba SDK: events are
bounded and lossy, snapshots are latest-value copies, and failed dials are
scheduled behind a finite retry deadline rather than blocking the UI.
"""

from dataclasses import dataclass
from collections import deque
import unittest


@dataclass
class Snapshot:
    state: str = "OFF"
    wifi: bool = False
    socket: bool = False
    last_rx: int = 0
    last_tx: int = 0


class RadioModel:
    def __init__(self, event_capacity=2):
        self.events = deque(maxlen=event_capacity)
        self.event_drops = 0
        self.snapshot = Snapshot()
        self.retry_at = 0

    def emit(self, event):
        if len(self.events) == self.events.maxlen:
            self.event_drops += 1
            return False
        self.events.append(event)
        return True

    def set_snapshot(self, **values):
        for key, value in values.items():
            setattr(self.snapshot, key, value)

    def failed_dial(self, now):
        self.set_snapshot(state="WAIT_WIFI", socket=False)
        self.retry_at = now + 3000

    def should_dial(self, now):
        return self.snapshot.wifi and self.snapshot.state == "WAIT_WIFI" and now >= self.retry_at


class C2RadioContractTests(unittest.TestCase):
    def test_event_queue_saturation_is_bounded_and_observable(self):
        radio = RadioModel()
        self.assertTrue(all(radio.emit(i) for i in range(2)))
        self.assertFalse(radio.emit(2))
        self.assertEqual(len(radio.events), 2)
        self.assertEqual(radio.event_drops, 1)

    def test_snapshot_is_latest_value_not_a_history_queue(self):
        radio = RadioModel()
        for sample in range(100):
            radio.set_snapshot(last_rx=sample)
        self.assertEqual(radio.snapshot.last_rx, 99)

    def test_reconnect_has_three_second_deadline(self):
        radio = RadioModel()
        radio.set_snapshot(wifi=True, state="CONNECTING")
        radio.failed_dial(1000)
        self.assertFalse(radio.should_dial(3999))
        self.assertTrue(radio.should_dial(4000))

    def test_wifi_loss_prevents_socket_retry(self):
        radio = RadioModel()
        radio.set_snapshot(wifi=False, state="WAIT_WIFI")
        radio.retry_at = 0
        self.assertFalse(radio.should_dial(10000))


if __name__ == "__main__":
    unittest.main()
