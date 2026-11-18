"""Guard the STM32 board composition root and run its BLE port bindings.

The board owns every concrete UI/hardware effect; Crabit::CompanionBle and
STM32Input only keep injected callbacks. The runtime half extracts the real
binding functions from board.cpp and compiles them against the real
companion_ble.h plus narrow fakes, so each hook forward is executed, not
grepped. No board init, HAL or hardware path is run.
"""

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_bw16_uart_ingress import extract_function

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "firmware/stm32/Core"
PRODUCT = ROOT / "firmware/stm32/product"
BOARD = PRODUCT / "board/board.cpp"
BLE_SRC = PRODUCT / "features/companion/companion_ble.cpp"
BLE_HEADER = PRODUCT / "features/companion/companion_ble.h"
INPUT_HEADER = CORE / "Inc/hw_input.h"

HOOK_FUNCTIONS = (
    "void launchBleBackground()",
    "void showBleToast(const char* message)",
    "void dismissPairPrompt()",
    "void invalidatePairPrompt(uint16_t session, uint32_t pair)",
    "void tickPairPrompt(uint16_t session, uint32_t pair)",
    "Crabit::CompanionBle::UiHooks companionUiHooks()",
    "Crabit::CompanionBle::HardwareHooks companionHardwareHooks()",
    "bool bindCompanionPorts()",
)


def prompt_declaration(source):
    match = re.search(
        r"^Crabit::CompanionPairPrompt pair_prompt\(\s*"
        r"Crabit::CompanionBle::confirm,\s*Crabit::CompanionBle::cancel\);\s*$",
        source,
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError("board.cpp has no single file-scope pair prompt")
    return match.group(0)


def order(text, needles, label):
    positions = []
    for needle in needles:
        if needle not in text:
            raise AssertionError(f"{label} is missing {needle!r}")
        positions.append((text.index(needle), needle))
    if positions != sorted(positions):
        raise AssertionError(f"{label} order is wrong: {positions}")
    return [position for position, _ in positions]


class BoardCompositionStructureTests(unittest.TestCase):
    def setUp(self):
        self.source = BOARD.read_text(encoding="utf-8")

    def test_board_is_the_only_owner_of_concrete_ui_and_hardware_ports(self):
        for include in (
            '#include "app_real/app_coproc.h"',
            '#include "app_real/app_cyber_link.h"',
            '#include "app_real/components/ble_controls.h"',
            '#include "app_real/components/companion_pair_prompt.h"',
        ):
            self.assertIn(include, self.source)
        self.assertEqual(self.source.count("pair_prompt("), 1)
        prompt_declaration(self.source)
        ble_source = BLE_SRC.read_text(encoding="utf-8")
        self.assertNotIn("CompanionPairPrompt", ble_source)
        self.assertNotIn("pair_prompt", ble_source)
        # Service headers stay free of the concrete app/prompt layer.
        ble_header = BLE_HEADER.read_text(encoding="utf-8")
        includes = re.findall(r'^\s*#include\s+[<"]([^>"]+)[>"]', ble_header, re.MULTILINE)
        for included in includes:
            self.assertNotIn("app_real", included)
            self.assertNotIn("NanoUI", included)
            self.assertNotIn("companion_pair_prompt", included)
            self.assertNotIn("app_coproc", included)
        self.assertNotRegex(ble_header, r"^\s*(class|struct)\s+CompanionPairPrompt")
        input_source = INPUT_HEADER.read_text(encoding="utf-8")
        self.assertNotIn("ble_controls", input_source)
        self.assertNotIn("toggleBleFromUi", input_source)

    def test_hook_sets_keep_the_agreed_mapping(self):
        ui = extract_function(self.source, "Crabit::CompanionBle::UiHooks companionUiHooks()")
        order(
            ui,
            (
                "&AppManager::frameAppActive",
                "&launchBleBackground",
                "&showBleToast",
                "&dismissPairPrompt",
                "&invalidatePairPrompt",
                "&tickPairPrompt",
            ),
            "UiHooks mapping",
        )
        self.assertIn("new CyberLinkApp(CyberLinkApp::Mode::BleBackground)", self.source)
        order(
            extract_function(self.source, "Crabit::CompanionBle::HardwareHooks companionHardwareHooks()"),
            ("&BW16_NormalBoot", "&BW16_ForceOff"),
            "HardwareHooks mapping",
        )

    def test_binding_only_stores_callbacks(self):
        body = extract_function(self.source, "bool bindCompanionPorts()")
        self.assertIn("Crabit::CompanionBle::bind(", body)
        for effect in ("BW16_NormalBoot()", "BW16_ForceOff()", "showToast", "launch(", "pair_prompt", "delay"):
            self.assertNotIn(effect, body)
        for wrapper in ("dismissPairPrompt", "invalidatePairPrompt", "tickPairPrompt"):
            text = extract_function(self.source, f"void {wrapper}")
            self.assertIn("pair_prompt.", text)

    def test_both_bindings_precede_input_service_and_ui_start(self):
        init_body = extract_function(self.source, "void Board::init()")
        set_pos, bind_pos = order(
            init_body,
            (
                "keys.setBleShortcutHandler(Crabit::toggleBleFromUi);",
                "if (!bindCompanionPorts())",
            ),
            "Board::init binding order",
        )
        self.assertIn("Fault::reset(Fault::Reason::Boot);", init_body[init_body.index("if (!bindCompanionPorts())"):])
        for later in (
            "keys.init();",
            "Crabit::initFeatures()",
            "AppServices::init(&wm);",
            "AppManager::launch(",
            "NanoUI_MainLoop",
        ):
            if later in init_body:
                self.assertGreater(init_body.index(later), max(set_pos, bind_pos), later)
        self.assertLess(bind_pos, init_body.index("Uart::startRx();"))
        tasks_body = extract_function(self.source, "void Board::startTasks()")
        self.assertNotIn("bindCompanionPorts", tasks_body)
        self.assertNotIn("setBleShortcutHandler", tasks_body)
        self.assertLess(self.source.index("void Board::init()"), self.source.index("void Board::startTasks()"))
        port = (CORE / "Src/nanoui_port.cpp").read_text(encoding="utf-8")
        order(port, ("Board::init();", "Board::startTasks();"), "boot call order")


class BoardCompositionRuntimeTests(unittest.TestCase):
    def test_real_binding_functions_forward_every_hook_once(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "existing host g++ is required")
        source = BOARD.read_text(encoding="utf-8")
        production = "\n".join(
            [prompt_declaration(source)] + [extract_function(source, signature) for signature in HOOK_FUNCTIONS]
        )
        program = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include "companion_ble.h"   // real UiHooks/HardwareHooks definitions

class Application { public: virtual ~Application() {} };
class CyberLinkApp : public Application {
 public:
  enum class Mode : uint8_t { Ui, Ble, BleBackground, Wifi };
  explicit CyberLinkApp(Mode selected = Mode::Ui) : mode(selected) {}
  Mode mode;
};
struct AppManager {
  static bool frame_value;
  static int frame_calls, launches;
  static CyberLinkApp::Mode last_mode;
  static bool frameAppActive() { ++frame_calls; return frame_value; }
  static void launch(Application* app) {
    ++launches;
    auto* cyber = static_cast<CyberLinkApp*>(app);
    last_mode = cyber->mode;
    delete app;
  }
};
bool AppManager::frame_value = false;
int AppManager::frame_calls = 0;
int AppManager::launches = 0;
CyberLinkApp::Mode AppManager::last_mode = CyberLinkApp::Mode::Ui;

struct Popup {
  static int toasts;
  static char last[48];
  static void showToast(const char* message, int = 1500) {
    ++toasts;
    std::strncpy(last, message, sizeof(last) - 1);
    last[sizeof(last) - 1] = '\0';
  }
};
int Popup::toasts = 0;
char Popup::last[48] = "";

int boots = 0, force_offs = 0;
void BW16_NormalBoot() { ++boots; }
void BW16_ForceOff() { ++force_offs; }

namespace Crabit {
class CompanionPairPrompt {
 public:
  using Command = bool (*)(uint16_t, uint32_t);
  CompanionPairPrompt(Command allow, Command deny) : allow_(allow), deny_(deny) {
    assert(allow_ && deny_);
  }
  void dismiss() { ++dismisses; }
  void invalidate(uint16_t session, uint32_t pair) {
    ++invalidations;
    invalidate_session = session;
    invalidate_pair = pair;
  }
  void tick(uint16_t session, uint32_t pair) {
    ++ticks;
    tick_session = session;
    tick_pair = pair;
  }
  static int dismisses, invalidations, ticks;
  static uint16_t invalidate_session, tick_session;
  static uint32_t invalidate_pair, tick_pair;
 private:
  Command allow_, deny_;
};
int CompanionPairPrompt::dismisses = 0;
int CompanionPairPrompt::invalidations = 0;
int CompanionPairPrompt::ticks = 0;
uint16_t CompanionPairPrompt::invalidate_session = 0, CompanionPairPrompt::tick_session = 0;
uint32_t CompanionPairPrompt::invalidate_pair = 0, CompanionPairPrompt::tick_pair = 0;
}  // namespace Crabit

namespace Crabit { namespace CompanionBle {
int binds = 0;
bool bind_result = true;
UiHooks seen_ui{};
HardwareHooks seen_hardware{};
bool bind(const UiHooks& ui, const HardwareHooks& hardware) {
  ++binds;
  seen_ui = ui;
  seen_hardware = hardware;
  return bind_result;
}
bool confirm(uint16_t, uint32_t) { return false; }
bool cancel(uint16_t, uint32_t) { return false; }
} }  // namespace Crabit::CompanionBle

namespace {
"""
            + production
            + r"""
}  // namespace

int main() {
  using Crabit::CompanionPairPrompt;
  auto ui = companionUiHooks();
  assert(ui.frameAppActive == &AppManager::frameAppActive);
  assert(ui.launchBleBackground == &launchBleBackground);
  assert(ui.showToast == &showBleToast);
  assert(ui.dismissPairPrompt == &dismissPairPrompt);
  assert(ui.invalidatePairPrompt == &invalidatePairPrompt);
  assert(ui.tickPairPrompt == &tickPairPrompt);
  assert(Popup::toasts == 0 && AppManager::launches == 0 && boots == 0 && force_offs == 0);

  AppManager::frame_value = true;
  assert(ui.frameAppActive() && AppManager::frame_calls == 1);
  AppManager::frame_value = false;
  assert(!ui.frameAppActive() && AppManager::frame_calls == 2);

  ui.launchBleBackground();
  assert(AppManager::launches == 1 && AppManager::last_mode == CyberLinkApp::Mode::BleBackground);
  assert(boots == 0 && force_offs == 0);

  ui.showToast("BLE STARTING");
  assert(Popup::toasts == 1 && std::strcmp(Popup::last, "BLE STARTING") == 0);
  ui.showToast("BLE BUSY");
  assert(Popup::toasts == 2 && std::strcmp(Popup::last, "BLE BUSY") == 0);

  ui.dismissPairPrompt();
  assert(CompanionPairPrompt::dismisses == 1);
  assert(CompanionPairPrompt::invalidations == 0 && CompanionPairPrompt::ticks == 0);
  const uint16_t invalidate_session = 7;
  const uint32_t invalidate_pair = 41;
  ui.invalidatePairPrompt(invalidate_session, invalidate_pair);
  assert(CompanionPairPrompt::invalidations == 1);
  assert(CompanionPairPrompt::invalidate_session == invalidate_session);
  assert(CompanionPairPrompt::invalidate_pair == invalidate_pair);
  assert(CompanionPairPrompt::dismisses == 1);
  ui.tickPairPrompt(9, 77);
  assert(CompanionPairPrompt::ticks == 1);
  assert(CompanionPairPrompt::tick_session == 9 && CompanionPairPrompt::tick_pair == 77);
  ui.tickPairPrompt(0, 0);
  assert(CompanionPairPrompt::ticks == 2 && CompanionPairPrompt::tick_session == 0 &&
         CompanionPairPrompt::tick_pair == 0);

  auto hardware = companionHardwareHooks();
  assert(hardware.normalBoot == &BW16_NormalBoot);
  assert(hardware.forceOff == &BW16_ForceOff);
  hardware.normalBoot();
  hardware.forceOff();
  assert(boots == 1 && force_offs == 1);

  // One boot-time install of the exact same sets; it never runs an effect.
  const int toasts = Popup::toasts, launches = AppManager::launches;
  const int boot_calls = boots, off_calls = force_offs;
  const int dismisses = CompanionPairPrompt::dismisses;
  assert(bindCompanionPorts());
  assert(Crabit::CompanionBle::binds == 1);
  assert(Crabit::CompanionBle::seen_ui.frameAppActive == ui.frameAppActive);
  assert(Crabit::CompanionBle::seen_ui.launchBleBackground == ui.launchBleBackground);
  assert(Crabit::CompanionBle::seen_ui.showToast == ui.showToast);
  assert(Crabit::CompanionBle::seen_ui.dismissPairPrompt == ui.dismissPairPrompt);
  assert(Crabit::CompanionBle::seen_ui.invalidatePairPrompt == ui.invalidatePairPrompt);
  assert(Crabit::CompanionBle::seen_ui.tickPairPrompt == ui.tickPairPrompt);
  assert(Crabit::CompanionBle::seen_hardware.normalBoot == &BW16_NormalBoot);
  assert(Crabit::CompanionBle::seen_hardware.forceOff == &BW16_ForceOff);
  assert(Popup::toasts == toasts && AppManager::launches == launches);
  assert(boots == boot_calls && force_offs == off_calls);
  assert(CompanionPairPrompt::dismisses == dismisses);

  // A rejected port set is reported so Board::init can reset the boot.
  Crabit::CompanionBle::bind_result = false;
  assert(!bindCompanionPorts() && Crabit::CompanionBle::binds == 2);
  assert(Popup::toasts == toasts && AppManager::launches == launches);
  assert(boots == boot_calls && force_offs == off_calls);
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-board-bindings-") as folder:
            cpp = Path(folder) / "board_bindings.cpp"
            binary = Path(folder) / ("board_bindings.exe" if os.name == "nt" else "board_bindings")
            cpp.write_text(program, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{CORE / 'Inc'}",
                    f"-I{PRODUCT / 'board'}",
                    f"-I{PRODUCT / 'features/companion'}",
                    str(cpp),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr + run.stdout)


if __name__ == "__main__":
    unittest.main()
