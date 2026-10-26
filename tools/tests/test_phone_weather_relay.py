"""Static integration guards, not BLE hardware acceptance."""

import re
import plistlib
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BW = ROOT / "firmware/rtl8720dn/NanoUI"


class PhoneWeatherRelayTests(unittest.TestCase):
    def test_standalone_weather_is_opt_in_and_ide_tools_remain(self):
        profile = (BW / "src/CrabitBuildProfile.h").read_text(encoding="utf-8")
        self.assertRegex(
            profile,
            r"#ifndef CRABIT_BW16_ENABLE_NETWORK_SYNC\s+"
            r"#define CRABIT_BW16_ENABLE_NETWORK_SYNC 0",
        )
        self.assertIn("#define CRABIT_BW16_PROFILE CRABIT_BW16_PROFILE_WIFI_TOOLS_LAB", profile)
        self.assertIn("#define CRABIT_BW16_IDE_HOPPER_TRIAL 1", profile)

    def test_correlated_business_reply_is_forwarded_to_ble(self):
        source = (BW / "src/main_entry.cpp").read_text(encoding="utf-8")
        allowlist = source[
            source.index('if (strcmp(message.topic, "ack")') : source.index(
                'if (strcmp(message.topic, "sys/mode")'
            )
        ]
        self.assertEqual(allowlist.count('strcmp(message.topic, "app/weather") == 0'), 1)
        self.assertIn("AmebaServices::enqueueNotification(message);", allowlist)

    def test_product_command_relay_stays_behind_existing_authentication(self):
        source = (BW / "RadioSvc.cpp").read_text(encoding="utf-8")
        body = source[
            source.index("void handleBleCommand(") : source.index("bool prepareBleWriteEvent(")
        ]
        self.assertRegex(
            body,
            re.compile(
                r"if \(!ble_is_authenticated\) return;.*"
                r"enqueueUartEvent\(UART_FORWARD, full_command.c_str\(\), 0\)",
                re.S,
            ),
        )

    def test_release_network_declarations_cover_weather_https(self):
        app = ROOT / "apps/mobile"
        manifest = ET.parse(app / "android/app/src/main/AndroidManifest.xml")
        names = {
            node.get("{http://schemas.android.com/apk/res/android}name")
            for node in manifest.getroot().findall("uses-permission")
        }
        self.assertIn("android.permission.INTERNET", names)
        for name in ("Release.entitlements", "DebugProfile.entitlements"):
            with (app / "macos/Runner" / name).open("rb") as source:
                self.assertIs(plistlib.load(source)["com.apple.security.network.client"], True)


if __name__ == "__main__":
    unittest.main()
