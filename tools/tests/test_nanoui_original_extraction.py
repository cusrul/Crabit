"""Portable-core boundaries plus replay of a completed integration phase.

Historical scope/mobile identity use fixed snapshots, not the evolving product.
Today's framework/app lifetime and layouts are exercised by CTest. Forwarders,
default-font body, product button composition and SDK independence stay checked
against today's sources. See docs/nanoui/test-contract.md.
"""

import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = "eaf49a1ae4dbe02900ddb7073d2f342568a9aa26"
# This authorization is narrower than the extraction-base compatibility scope.
BW16_PROFILE_BASE = "922b4502c94add90feeca2196b7730120c6848bf"
DISPLAY_CALLBACK_BASE = "112825573d8ad6a853c897c6623db1721694c4d4"
SETTINGS_FLAT_BASE = "19de38126450c65f7e15515c933ead2cb6a5d857"
MOBILE_TRANSPORT_BASE = "91062f2d7f4bf60d9003b8a7ed9699d9fa0ec107"
# Replay the completed integration phase, not every future product feature.
INTEGRATION_SNAPSHOT = "3e838bf"
MOBILE_TRANSPORT_PATHS = {
    "apps/mobile/README.md",
    "apps/mobile/lib/core/diagnostics/ble_regression_adapter.dart",
    "apps/mobile/lib/core/diagnostics/transport_automation.dart",
    "apps/mobile/lib/core/diagnostics/transport_regression.dart",
    "apps/mobile/lib/core/managers/ble_manager.dart",
    "apps/mobile/lib/core/transport/ble_transport.dart",
    "apps/mobile/lib/presentation/pages/misc/transport_diagnostics_page.dart",
    "apps/mobile/lib/transport_diagnostics_main.dart",
    "apps/mobile/test/ble_transport_test.dart",
    "apps/mobile/test/transport_diagnostics_page_test.dart",
    "apps/mobile/test/transport_regression_test.dart",
}
MOBILE_DEVICE_CENTER_PATHS = {
    "apps/mobile/lib/core/device/device_link.dart",
    "apps/mobile/lib/core/device/device_center_controller.dart",
    "apps/mobile/lib/presentation/pages/home/dashboard_page.dart",
    "apps/mobile/lib/presentation/pages/home/home_page.dart",
    "apps/mobile/test/device_center_controller_test.dart",
    "apps/mobile/test/device_center_page_test.dart",
    "apps/mobile/test/device_center_render_test.dart",
    "apps/mobile/test/device_link_test.dart",
    "apps/mobile/test/support/fake_device_link.dart",
}
MOBILE_VISUAL_PATHS = {
    "apps/mobile/lib/main.dart",
    "apps/mobile/lib/core/theme/app_theme.dart",
    "apps/mobile/lib/presentation/widgets/app_shell.dart",
    "apps/mobile/lib/presentation/widgets/app_page.dart",
    "apps/mobile/lib/presentation/widgets/watch_illustration.dart",
    "apps/mobile/lib/presentation/pages/settings/settings_page.dart",
    "apps/mobile/lib/presentation/pages/misc/debug_page.dart",
    "apps/mobile/lib/presentation/pages/tools/badusb_page.dart",
    "apps/mobile/lib/presentation/pages/ble/sniffer_page.dart",
    "apps/mobile/test/app_shell_test.dart",
}
# User-approved AMEBA_LINK-only FRAME migration. Hardware helpers, other Apps,
# BW16 and the portable framework are not opened by this exact-path allowance.
AMEBA_FRAME_PATHS = {
    "firmware/stm32/Core/NanoUI/app_real/app_cyber_link.h",
    "firmware/stm32/Core/Src/nanoui_port.cpp",
    "firmware/stm32/Core/Inc/event_bus.h",
    "firmware/stm32/Core/Inc/ameba_session.h",
}
AMEBA_FRAME_DOC_PATHS = {"firmware/stm32/README.md"}
# Separately authorized minimal CDC + fixed-keyboard prototype. No HAL, boot,
# power, linker, flash bridge, other middleware or portable-framework exception.
USB_COMPOSITE_PATHS = {
    "firmware/stm32/USB_DEVICE/App/usb_device.c",
    "firmware/stm32/USB_DEVICE/App/usbd_desc.c",
    "firmware/stm32/USB_DEVICE/App/usbd_cdc_if.c",
    "firmware/stm32/USB_DEVICE/Target/usbd_conf.c",
    "firmware/stm32/USB_DEVICE/Target/usbd_conf.h",
    "firmware/stm32/USB_DEVICE/Composite/usb_composite_config.h",
    "firmware/stm32/USB_DEVICE/Composite/usb_composite.h",
    "firmware/stm32/USB_DEVICE/Composite/usb_composite.c",
    "firmware/stm32/USB_DEVICE/Composite/usb_keyboard.h",
    "firmware/stm32/USB_DEVICE/Composite/usb_keyboard.c",
    "firmware/stm32/USB_DEVICE/Composite/usb_diag.h",
    "firmware/stm32/USB_DEVICE/Composite/usb_diag.c",
    "firmware/stm32/cmake/stm32cubemx/CMakeLists.txt",
    "firmware/stm32/Middlewares/ST/STM32_USB_Device_Library/Class/CompositeBuilder/Inc/usbd_composite_builder.h",
    "firmware/stm32/Middlewares/ST/STM32_USB_Device_Library/Class/CompositeBuilder/Src/usbd_composite_builder.c",
    "firmware/stm32/Middlewares/ST/STM32_USB_Device_Library/Class/CustomHID/Inc/usbd_customhid.h",
    "firmware/stm32/Middlewares/ST/STM32_USB_Device_Library/Class/CustomHID/Src/usbd_customhid.c",
}
BW16_PROFILE_PATHS = {
    "firmware/rtl8720dn/NanoUI/src/CrabitBuildProfile.h",
    "firmware/rtl8720dn/NanoUI/app/launcher.h",
}
DISPLAY_PRODUCT_PATHS = {
    "firmware/stm32/Core/NanoUI/app_real/app_settings.h",
    "firmware/stm32/Core/NanoUI/app_real/components/action_button.h",
}
SETTINGS_FLAT_PATHS = {
    "firmware/stm32/Core/NanoUI/app_real/launcher.h",
    "firmware/stm32/Core/NanoUI/app_real/components/settings_option_picker.h",
}
WIDGETS = (
    "basic",
    "container",
    "button",
    "text",
    "image",
    "animated_image",
    "transition",
    "image_menu",
)
PORTS = (
    "firmware/stm32/Core/NanoUI/include/NanoUI/widgets",
    "firmware/rtl8720dn/NanoUI/src/widgets",
)
PIECES = tuple(f"widgets/{name}" for name in WIDGETS) + (
    "hal/canvas",
    "core/default_font",
    "framework/app",
)
ROOTS = tuple(str(Path(port).parent).replace("\\", "/") for port in PORTS)
UNCHANGED_PIECES = ("core/default_font", "framework/app")


def integration_paths():
    """Exact original-unification allowlist; no directory-wide exceptions."""
    allowed = {f"{p}/{n}.h" for p in ROOTS for n in PIECES}
    extra = (
        "core/geometry",
        "core/animator",
        "core/memory_pool",
        "core/embedded_vector",
        "core/text",
        "core/callback",
        "core/fixed_state",
        "core/window_manager",
        "framework/widget",
        "framework/app_manager",
        "rendering/render_object",
        "hal/input",
        "hal/hal_time",
        "hal/font_engine",
    )
    widgets = ("flex", "scroll", "vbox", "carousel", "modern_menu", "controls", "popup")
    allowed.update(f"{p}/{n}.h" for p in ROOTS for n in extra)
    allowed.update(f"{p}/widgets/{n}.h" for p in ROOTS for n in widgets)
    allowed.update(
        (
            "firmware/stm32/Core/NanoUI/CMakeLists.txt",
            "firmware/rtl8720dn/NanoUI/src/nanoui_statics.cpp",
            "firmware/stm32/Core/NanoUI/platforms/desktop/main.cpp",
        )
    )
    return allowed


def integration_phase_paths():
    allowed = integration_paths()
    # 2026-10-21: authorized UTF-8 convergence changes only these providers;
    # actual provider execution is covered by test_utf8_font_providers.py.
    allowed.update(
        {
            "firmware/stm32/Core/NanoUI/src/font_engine.cpp",
            "firmware/rtl8720dn/NanoUI/src/font_engine.cpp",
        }
    )
    desktop = "firmware/stm32/Core/NanoUI"
    # Approved resource tooling and desktop-specific generated subset only.
    allowed.update(
        f"{desktop}/platforms/desktop/{name}"
        for name in ("nanoui-assets.json", "generate_resources.py", "README.md")
    )
    allowed.update(
        (
            f"{desktop}/assets/generated/desktop/nanoui_assets.cpp",
            f"{desktop}/assets/generated/desktop/NanoUI/nanoui_assets.h",
            f"{desktop}/assets/generated/desktop/.gitignore",
        )
    )
    for root in ("firmware/stm32/Core/NanoUI", "firmware/rtl8720dn/NanoUI"):
        allowed.update(
            (
                f"{root}/tools/font_builder.py",
                f"{root}/tools/asset_builder.py",
                f"{root}/nanoui-assets.json",
            )
        )
    # b5ebc32: SDL event translation and software-render readback diagnostics.
    # This does not allow board adapters or any firmware startup/service source.
    allowed.update(
        f"{desktop}/platforms/desktop/{name}" for name in ("sdl_input.h", "sdl_adapter.h")
    )
    # The separately validated BW16 measurement-profile contract is exact-path only.
    allowed.update(BW16_PROFILE_PATHS)
    # Explicitly authorized STM32 product consumer sample; no directory-wide exception.
    allowed.update(DISPLAY_PRODUCT_PATHS)
    allowed.update(SETTINGS_FLAT_PATHS)
    allowed.update(MOBILE_TRANSPORT_PATHS)
    allowed.update(MOBILE_DEVICE_CENTER_PATHS)
    allowed.update(MOBILE_VISUAL_PATHS)
    allowed.update(AMEBA_FRAME_PATHS)
    allowed.update(AMEBA_FRAME_DOC_PATHS)
    allowed.update(USB_COMPOSITE_PATHS)
    return allowed


def original(path, revision=BASE):
    return (
        subprocess.check_output(["git", "show", f"{revision}:{path}"], cwd=ROOT)
        .decode("utf-8")
        .replace("\r\n", "\n")
    )


def implementation_body(text):
    body = re.sub(r"^#include .*\n", "", text, flags=re.M).rstrip()
    # Ignore formatter whitespace, but never erase whitespace inside C++ literals.
    return re.sub(
        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\s+',
        lambda match: match.group() if match.group()[0] in "\"'" else "",
        body,
    )


def original_body(name, text):
    if name == "core/default_font":
        text = re.sub(
            r"#ifdef ARDUINO\n.*?#endif",
            "#ifndef PROGMEM\n  #define PROGMEM\n#endif\n"
            "#ifndef FONT_READ\n  #define FONT_READ(addr) (*(addr))\n#endif",
            text,
            flags=re.S,
        )
    return implementation_body(text)


def historical_changed_product_paths():
    tracked = subprocess.check_output(
        [
            "git",
            "diff",
            "--name-only",
            "--no-renames",
            BASE,
            INTEGRATION_SNAPSHOT,
            "--",
            "firmware",
            "apps",
        ],
        cwd=ROOT,
        text=True,
    ).splitlines()
    return set(tracked)


class OriginalExtractionTests(unittest.TestCase):
    def test_historical_mobile_transport_stays_at_fixed_verified_source(self):
        for path in MOBILE_TRANSPORT_PATHS:
            with self.subTest(path=path):
                self.assertEqual(
                    original(path, INTEGRATION_SNAPSHOT), original(path, MOBILE_TRANSPORT_BASE)
                )

    def test_unchanged_bodies_are_original_on_both_ports(self):
        for name in UNCHANGED_PIECES:
            with self.subTest(widget=name):
                sources = [original(f"{port}/{name}.h") for port in ROOTS]
                self.assertEqual(sources[0], sources[1])
                shared = ROOT / f"libraries/NanoUI/src/NanoUI/product/{name}.h"
                self.assertEqual(
                    implementation_body(shared.read_text(encoding="utf-8")),
                    original_body(name, sources[0]),
                )

    def test_forwarders_preserve_platform_dependencies(self):
        for name in PIECES:
            for port in ROOTS:
                with self.subTest(widget=name, port=port):
                    path = f"{port}/{name}.h"
                    current = (ROOT / path).read_text(encoding="utf-8")
                    self.assertIn(f"#include <NanoUI/product/{name}.h>", current)
                    self.assertNotIn("class ", current)

    def test_historical_framework_integration_scope(self):
        unexpected = historical_changed_product_paths() - integration_phase_paths()
        self.assertFalse(unexpected, unexpected)

    def test_historical_display_callback_and_action_button_recover_fixed_product_source(self):
        path = "firmware/stm32/Core/NanoUI/app_real/app_settings.h"
        # Preserve the historical callback extraction proof at its final baseline.
        # The authorized flat page is now exercised by test_stm32_settings_flat.py;
        # it must not be forced back into the superseded multi-App source shape.
        current = original(path, SETTINGS_FLAT_BASE)
        expected = original(path, DISPLAY_CALLBACK_BASE)
        reversals = (
            ('#include "components/action_button.h"\n', ""),
            (
                "    bool invert_disp = false;",
                "    static inline DisplayConfApp* instance = nullptr; \n    bool invert_disp = false;",
            ),
            (
                "    void onBrightChange(float v) {\n        brightness = v;",
                "    static void onBrightChange(float v) {\n        if (!instance) return;\n        instance->brightness = v;",
            ),
            (
                "    void onInvertChange(bool v) {\n        invert_disp = v;",
                "    static void onInvertChange(bool v) {\n        if (!instance) return;\n        instance->invert_disp = v;",
            ),
            (
                "    void onSave() {\n        sys_cfg.invert_disp = invert_disp;\n        sys_cfg.brightness = brightness;",
                "    static void onSave() {\n        if (!instance) return;\n        sys_cfg.invert_disp = instance->invert_disp; \n        sys_cfg.brightness = instance->brightness; ",
            ),
            (
                "        invert_disp = sys_cfg.invert_disp;",
                "        instance = this; \n        invert_disp = sys_cfg.invert_disp;",
            ),
            (
                "new Slider(brightness, 54, 64, [this](float v) { onBrightChange(v); })",
                "new Slider(brightness, 54, 64, onBrightChange)",
            ),
            (
                "new Switch(invert_disp, [this](bool v) { onInvertChange(v); })",
                "new Switch(invert_disp, onInvertChange)",
            ),
            (
                'new ActionButton(" APPLY ", [this]() { onSave(); })',
                'new Button(new Container(new Text(" APPLY "), 46, 16, 0, true, 2), onSave)',
            ),
            (
                'new ActionButton(" APPLY ", onSave)',
                'new Button(new Container(new Text(" APPLY "), 46, 16, 0, true, 2), onSave)',
            ),
            (
                'new ActionButton(" BACK ", [](){AppManager::exitCurrentApp();})',
                'new Button(new Container(new Text(" BACK "), 46, 16, 0, true, 2), [](){AppManager::exitCurrentApp();})',
            ),
            (
                'new ActionButton(" EXIT ", [](){ AppManager::exitCurrentApp(); })',
                'new Button(new Container(new Text(" EXIT "), 46, 16, 0, true, 2), [](){ AppManager::exitCurrentApp(); })',
            ),
        )
        recovered = current.replace(
            "new Slider(brightness, 54, 64, [this](float v) { onBrightChange(v); })));\n",
            "new Slider(brightness, 54, 64, [this](float v) { onBrightChange(v); }))); \n",
        )
        for changed, baseline in reversals:
            self.assertIn(changed, recovered, f"missing authorized form: {changed}")
            recovered = recovered.replace(changed, baseline)
        self.assertEqual(recovered, expected)
        fixed_bytes = subprocess.check_output(
            ["git", "show", f"{DISPLAY_CALLBACK_BASE}:{path}"], cwd=ROOT
        )
        self.assertEqual(
            recovered.encode("utf-8").replace(b"\n", b"\r\n"),
            fixed_bytes.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"),
            "reversing edits must reproduce the fixed blob byte-for-byte apart from checkout EOL",
        )

    def test_action_button_is_exact_minimal_shared_container_wrapper(self):
        path = ROOT / "firmware/stm32/Core/NanoUI/app_real/components/action_button.h"
        self.assertEqual(
            implementation_body(path.read_text(encoding="utf-8")),
            implementation_body(
                """#ifndef APP_REAL_COMPONENTS_ACTION_BUTTON_H
#define APP_REAL_COMPONENTS_ACTION_BUTTON_H

#include <NanoUI.h>

class ActionButton : public Button {
public:
    ActionButton(const char* text, NanoUI::Callback<void()> callback)
        : Button(new Container(new Text(text, 8), 46, 16, 0, true, 2), callback) {}
};

#endif
"""
            ),
        )

    def test_portable_source_has_no_sdk_or_reverse_product_includes(self):
        for path in (ROOT / "libraries/NanoUI/src").rglob("*"):
            if path.suffix not in (".h", ".cpp"):
                continue
            text = path.read_text(encoding="utf-8")
            includes = re.findall(r'^\s*#\s*include\s+[<"]([^>"]+)', text, flags=re.M)
            for dep in includes:
                with self.subTest(path=str(path), include=dep):
                    self.assertNotRegex(
                        dep.lower(),
                        r"arduino|stm32|freertos|zephyr|sdl|tracy|avr/|firmware|app_real|sdk",
                    )


if __name__ == "__main__":
    unittest.main()
