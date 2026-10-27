"""Guard visible identity without migrating package IDs or device behavior."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class AppIdentityTests(unittest.TestCase):
    def test_visible_names(self):
        self.assertIn('L"Crabit"', (ROOT/'apps/mobile/windows/runner/main.cpp').read_text())
        self.assertIn('android:label="Crabit"', (ROOT/'apps/mobile/android/app/src/main/AndroidManifest.xml').read_text())
        self.assertIn("title: 'Crabit'", (ROOT/'apps/mobile/lib/main.dart').read_text())

    def test_user_data_not_deleted(self):
        script = (ROOT/'tools/crabit-windows.iss').read_text()
        self.assertIn('PrivilegesRequired=lowest',script)
        self.assertNotIn('[UninstallDelete]',script)
        self.assertNotIn('[InstallDelete]',script)
        self.assertIn('skipifsilent',script)
        self.assertIn('InfoBeforeFile=windows-installation.txt',script)

    def test_icons_have_expected_file_headers(self):
        self.assertEqual((ROOT/'apps/mobile/windows/runner/resources/app_icon.ico').read_bytes()[:4],b'\x00\x00\x01\x00')
        for density in ('mdpi','hdpi','xhdpi','xxhdpi','xxxhdpi'):
            icon=ROOT/f'apps/mobile/android/app/src/main/res/mipmap-{density}/ic_launcher.png'
            self.assertEqual(icon.read_bytes()[:8],b'\x89PNG\r\n\x1a\n')

if __name__ == '__main__':
    unittest.main()
