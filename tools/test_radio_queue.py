"""Host checks for the BW16 BLE owner boundary.

The AmebaD SDK is not available on the host, so the transition tests model
only the bounded owner contract.  The source checks cover the part that must
remain true in the Arduino build: the product core submits requests to
RadioSvc and does not call BLE SDK APIs directly.
"""

from collections import deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NANOUI = ROOT / "firmware" / "rtl8720dn" / "NanoUI"
STM32_COPROC = ROOT / "firmware" / "stm32" / "Core" / "NanoUI" / "app_real" / "app_coproc.h"
BUILD_PROFILE = NANOUI / "src" / "CrabitBuildProfile.h"
LAUNCHER = NANOUI / "app" / "launcher.h"
BLE_SPAM = NANOUI / "ble_spam.cpp"


class OwnerModel:
    IDLE = "IDLE"
    STARTING = "STARTING"
    ADVERTISING = "ADVERTISING"
    CONNECTED = "CONNECTED"
    STOPPING = "STOPPING"
    FAILED = "FAILED"
    TIMEOUT = "TIMEOUT"

    QUEUED = "QUEUED"
    ACQUIRED = "ACQUIRED"
    BUSY = "BUSY"
    REJECTED = "REJECTED"
    FAILED = "FAILED"
    STOPPED = "STOPPED"

    def __init__(self, command_capacity=4, start_timeout_ms=5000):
        self.commands = deque(maxlen=command_capacity)
        self.state = self.IDLE
        self.generation = 0
        self.request_id = 0
        self.deadline = 0
        self.start_timeout_ms = start_timeout_ms
        self.last_result = self.STOPPED

    def request_start(self, request_id=0, now=0):
        if self.state in {
            self.STARTING,
            self.ADVERTISING,
            self.CONNECTED,
            self.STOPPING,
        }:
            return self.BUSY
        self.generation += 1
        command = ("start", self.generation, request_id)
        if len(self.commands) == self.commands.maxlen:
            self.last_result = self.REJECTED
            return self.REJECTED
        self.request_id = request_id
        self.deadline = now + self.start_timeout_ms
        self.state = self.STARTING
        self.last_result = self.QUEUED
        self.commands.append(command)
        return self.QUEUED

    def request_stop(self, request_id=0):
        if self.state in {self.IDLE, self.FAILED, self.TIMEOUT}:
            self.last_result = self.STOPPED
            return self.STOPPED
        self.generation += 1
        if len(self.commands) == self.commands.maxlen:
            self.last_result = self.REJECTED
            return self.REJECTED
        self.state = self.STOPPING
        self.commands.append(("stop", self.generation, request_id))
        return self.QUEUED

    def process_one(self):
        if not self.commands:
            return None
        kind, generation, request_id = self.commands.popleft()
        if generation != self.generation:
            return "STALE"
        if kind == "start":
            self.state = self.ADVERTISING
            self.last_result = self.ACQUIRED
            return self.ACQUIRED
        self.state = self.IDLE
        self.last_result = self.STOPPED
        return self.STOPPED

    def fail(self):
        self.state = self.FAILED
        self.last_result = self.FAILED

    def connect(self):
        assert self.state == self.ADVERTISING
        self.state = self.CONNECTED

    def tick(self, now):
        if self.state == self.STARTING and now >= self.deadline:
            self.generation += 1
            self.state = self.TIMEOUT
            return "TIMEOUT"
        return None


class PairDiagModel:
    def __init__(self):
        self.active = False
        self.pending = False
        self.persist_calls = 0
        self.state = "OFF"

    def begin(self):
        self.active = True
        self.pending = False
        self.state = "IDLE"

    def receive_pair(self, frame_length):
        assert self.active
        if frame_length == 37:
            self.pending = True
            self.state = "WAIT"

    def confirm_ram(self):
        if not self.active or not self.pending:
            return "REJECTED"
        self.pending = False
        self.state = "RAM OK"
        return "PAIR_CONFIRMED"

    def cancel(self):
        self.pending = False
        self.state = "CANCEL"


def test_bounded_event_queue_drops_without_blocking():
    queue = deque(maxlen=2)
    drops = 0
    for item in ("ble", "wifi", "extra"):
        if len(queue) == queue.maxlen:
            drops += 1
        else:
            queue.append(item)
    assert list(queue) == ["ble", "wifi"]
    assert drops == 1


def test_owner_state_transitions_and_busy_result():
    owner = OwnerModel()
    assert owner.request_start(7) == owner.QUEUED
    assert owner.request_start(8) == owner.BUSY
    assert owner.process_one() == owner.ACQUIRED
    assert owner.state == owner.ADVERTISING
    owner.connect()
    assert owner.state == owner.CONNECTED
    assert owner.request_stop(9) == owner.QUEUED
    assert owner.process_one() == owner.STOPPED
    assert owner.state == owner.IDLE


def test_timeout_invalidates_old_generation():
    owner = OwnerModel()
    assert owner.request_start(1, now=100) == owner.QUEUED
    old_generation = owner.generation
    assert owner.tick(5100) == "TIMEOUT"
    assert owner.state == owner.TIMEOUT
    assert owner.generation != old_generation
    assert owner.process_one() == "STALE"
    assert owner.state == owner.TIMEOUT
    assert owner.request_start(2, now=5200) == owner.QUEUED


def test_notification_queue_rejects_when_full():
    queue = deque(maxlen=4)
    for item in range(4):
        queue.append(item)
    assert len(queue) == 4
    assert len(queue) == queue.maxlen
    # The firmware uses xQueueSend(..., 0), so saturation is observable and
    # never turns into a blocking wait.
    result = "REJECTED" if len(queue) == queue.maxlen else "QUEUED"
    assert result == "REJECTED"


def test_failed_owner_can_be_retried_without_reusing_generation():
    owner = OwnerModel()
    assert owner.request_start(3) == owner.QUEUED
    owner.fail()
    failed_generation = owner.generation
    assert owner.request_start(4) == owner.QUEUED
    assert owner.generation != failed_generation


def test_command_queue_saturation_is_rejected():
    owner = OwnerModel()
    owner.commands.extend(("stale", 100 + item, 0) for item in range(4))
    assert owner.request_start(5) == owner.REJECTED
    assert owner.state == owner.IDLE
    assert owner.last_result == owner.REJECTED


def test_pair_diag_ram_confirmation_never_persists():
    pair = PairDiagModel()
    pair.begin()
    pair.receive_pair(20)
    assert pair.state == "IDLE"
    pair.receive_pair(37)
    assert pair.state == "WAIT"
    assert pair.confirm_ram() == "PAIR_CONFIRMED"
    assert pair.persist_calls == 0
    pair.cancel()
    assert pair.state == "CANCEL"


def test_product_core_has_one_ble_sdk_owner():
    owner = (NANOUI / "RadioSvc.cpp").read_text(encoding="utf-8")
    core_files = [
        NANOUI / "NetManager.cpp",
        NANOUI / "NetManager.h",
        NANOUI / "AmebaServices.h",
        NANOUI / "src" / "main_entry.cpp",
        NANOUI / "app" / "launcher.h",
    ]
    sdk_tokens = (
        "BLE.init(",
        "BLE.end(",
        "BLE.beginPeripheral(",
        "BLE.configAdvert(",
        "BLE.addService(",
        "BLECharacteristic ",
        "BLEService ",
    )
    assert "BLE.init(" in owner
    assert "BLE.beginPeripheral(" in owner
    assert "setWriteCallback(bleWriteCallback)" in owner
    assert "BLE.end(" not in owner
    assert "void service()" in owner
    assert "Protocol::send_to_stm32(event.payload)" in owner
    for path in core_files:
        source = path.read_text(encoding="utf-8")
        assert not any(token in source for token in sdk_tokens), path
    services = (NANOUI / "AmebaServices.h").read_text(encoding="utf-8")
    assert "NetManager::stopBLE()" in services
    assert "RadioSvc::init()" in services


def test_product_profile_excludes_legacy_ble_sdk_owners():
    profile = BUILD_PROFILE.read_text(encoding="utf-8")
    launcher = LAUNCHER.read_text(encoding="utf-8")
    spam = BLE_SPAM.read_text(encoding="utf-8")

    # Arduino compiles sketch-root .cpp files even when the product never
    # launches their App. Keep their implementation under a false product
    # profile gate, and include HID Apps only in their explicit lab profile.
    assert "#define CRABIT_BW16_PROFILE_PRODUCT 0" in profile
    assert "#define CRABIT_BW16_ENABLE_BLE_SPAM 0" in profile
    assert '#if CRABIT_BW16_IS_BLE_MOUSE_LAB\n#include "app_ble_mouse.h"' in launcher
    assert '#elif CRABIT_BW16_IS_BLE_PPT_LAB\n#include "app_ble_ppt.h"' in launcher
    assert '#elif CRABIT_BW16_IS_BLE_KBD_LAB\n#include "app_ble_kbd_test.h"' in launcher
    assert '#include "src/CrabitBuildProfile.h"\n\n#if CRABIT_BW16_ENABLE_BLE_SPAM' in spam


def test_compatibility_facade_is_queue_only():
    net = (NANOUI / "NetManager.cpp").read_text(encoding="utf-8")
    assert "RadioSvc::requestStart()" in net
    assert "RadioSvc::requestStop()" in net
    assert "RadioSvc::enqueueNotify" in net
    assert "delay(30)" not in net


def test_boot_ble_mode_cannot_take_over_product_ui():
    services = (NANOUI / "AmebaServices.h").read_text(encoding="utf-8")
    expected_gate = 'v_str == "BLE" && ' "current_mode == MODE_WAITING"
    assert expected_gate in services


def test_product_advertisement_has_the_product_name():
    radio = (NANOUI / "RadioSvc.cpp").read_text(encoding="utf-8")
    assert 'advert_data.addCompleteName("Crabit-Watch")' in radio
    assert "BLE.configAdvert()->setAdvData(advert_data)" in radio
    assert 'BLE.setDeviceName("Crabit-Watch")' in radio


def test_disconnect_restarts_advertising_in_radio_owner():
    radio = (NANOUI / "RadioSvc.cpp").read_text(encoding="utf-8")
    disconnect_marker = "clearConnectionState();\n    if (g_peripheral_started) BLE.configAdvert()->startAdv();\n    setState(RadioSvc::ADVERTISING);"
    assert disconnect_marker in radio


def test_pair_command_waits_for_flutter_second_chunk():
    token = "0123456789abcdef0123456789abcdef"
    command = f"PAIR:{token}"
    first_chunk = command[:20]
    second_chunk = command[20:]
    assert len(command) == 37
    assert len(first_chunk) == 20
    assert len(second_chunk) == 17

    radio = (NANOUI / "RadioSvc.cpp").read_text(encoding="utf-8")
    assert "PAIR_TOKEN_HEX_LENGTH = 32" in radio
    assert "PAIR_COMMAND_LENGTH = 5 + PAIR_TOKEN_HEX_LENGTH" in radio
    assert "return g_rx_length >= PAIR_COMMAND_LENGTH;" in radio
    assert "if (!isPairTokenValid(token)) return;" in radio
    assert "g_rx_buffer[0] = '\\0';" in radio

    # Flutter writes the current challenge in two GATT requests. The first
    # request must remain incomplete; only the 20 + 17 reconstruction is a
    # valid PAIR command.
    assert len(first_chunk) < len(command)
    assert len(first_chunk + second_chunk) == len(command)


def test_ble_test_screen_renders_existing_pair_status():
    coproc = STM32_COPROC.read_text(encoding="utf-8")
    assert "const char* bleStatusLabel() const" in coproc
    assert '"PAIR WAIT: PRESS OK"' in coproc
    assert '"PAIR CONFIRMED"' in coproc
    assert '"PAIR TIMEOUT"' in coproc
    assert "canvas->drawText(10, 36, bleStatusLabel(), 1);" in coproc


def test_pair_diag_probe_uses_flutter_wire_chunks_and_ram_confirmation():
    probe = (ROOT / "tools" / "bw16_pair_probe.py").read_text(encoding="utf-8")
    assert "first_chunk = command[:20]" in probe
    assert "second_chunk = command[20:]" in probe
    assert "PAIR_FIRST_FRAGMENT_SILENT" in probe
    assert "PAIR_DIAG_WAIT_OK action=PRESS_RAM_OK" in probe
    assert "PAIR_RAM_CONFIRM_OK" in probe
    assert "PAIR_READVERTISE_OK" in probe
    assert "persistence=false" in probe


def test_pair_diag_is_queue_only_and_never_writes_preferences():
    radio = (NANOUI / "RadioSvc.cpp").read_text(encoding="utf-8")
    launcher = LAUNCHER.read_text(encoding="utf-8")

    assert "class PairDiagApp" in launcher
    assert "RadioSvc::beginPairDiagnostic()" in launcher
    assert "RadioSvc::confirmPairInRam()" in launcher
    assert "RadioSvc::cancelPairDiagnostic()" in launcher
    assert "PAIR DIAG" in launcher
    assert "BLE.init(" not in launcher
    assert "Preferences" not in launcher
    assert "COMMAND_PAIR_DIAG_CONFIRM_RAM" in radio
    assert "void processPairDiagnosticConfirmRam()" in radio
    diag_confirm = radio.split("void processPairDiagnosticConfirmRam()", 1)[1].split(
        "void processPairDiagnosticCancel()", 1
    )[0]
    assert "putString" not in diag_confirm
    assert (
        "if (g_pair_diagnostic_active) return;\n    if (digitalRead(BTN_OK_PIN) != LOW) return;"
        in radio
    )
    assert radio.count('g_pairing_prefs.putString("pair_token", g_bound_pair_token);') == 1


def test_pair_diag_layout_is_bounded_to_the_oled():
    launcher = LAUNCHER.read_text(encoding="utf-8")
    pair_diag = launcher.split("class PairDiagApp", 1)[1].split(
        "// ==========================================", 1
    )[0]
    assert "static constexpr uint8_t CONTENT_HEIGHT" in pair_diag
    assert "LINE_HEIGHT * 4 + VERTICAL_GAP + BUTTON_HEIGHT" in pair_diag
    assert 'static_assert(CONTENT_HEIGHT <= 64, "PAIR DIAG must fit the 64px OLED");' in pair_diag
    assert "new Text(action_line" not in pair_diag
    assert 'new Text(" BACK "' not in pair_diag


if __name__ == "__main__":
    tests = (
        test_bounded_event_queue_drops_without_blocking,
        test_owner_state_transitions_and_busy_result,
        test_timeout_invalidates_old_generation,
        test_notification_queue_rejects_when_full,
        test_failed_owner_can_be_retried_without_reusing_generation,
        test_command_queue_saturation_is_rejected,
        test_pair_diag_ram_confirmation_never_persists,
        test_product_core_has_one_ble_sdk_owner,
        test_product_profile_excludes_legacy_ble_sdk_owners,
        test_compatibility_facade_is_queue_only,
        test_boot_ble_mode_cannot_take_over_product_ui,
        test_product_advertisement_has_the_product_name,
        test_disconnect_restarts_advertising_in_radio_owner,
        test_pair_command_waits_for_flutter_second_chunk,
        test_ble_test_screen_renders_existing_pair_status,
        test_pair_diag_probe_uses_flutter_wire_chunks_and_ram_confirmation,
        test_pair_diag_is_queue_only_and_never_writes_preferences,
        test_pair_diag_layout_is_bounded_to_the_oled,
    )
    for test in tests:
        test()
    print("radio owner queue/state/ownership checks passed")
