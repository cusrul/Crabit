"""Keep retired snapshots out without removing live USB or desktop ports."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class RetiredStm32SourcesTests(unittest.TestCase):
    def test_retired_snapshots_are_absent(self):
        for relative in ('usb_hid','usb_hid_new','Core/NanoUI/app_new',
                         'Core/NanoUI/examples','Core/NanoUI/CMakeLists_STM32.txt',
                         'Core/NanoUI/platforms/desktop/main copy'):
            path=ROOT/'firmware/stm32'/relative
            self.assertFalse(path.is_file() or any(path.rglob('*')) if path.exists() else False, str(path))

    def test_active_usb_and_desktop_are_retained(self):
        for relative in ('USB_DEVICE/Composite/usb_composite.c','USB_DEVICE/Composite/usb_keyboard.c',
                         'USB_DEVICE/Composite/usb_script.c',
                         'Middlewares/ST/STM32_USB_Device_Library/Class/CustomHID/Src/usbd_customhid.c',
                         'Core/NanoUI/platforms/desktop/main.cpp'):
            self.assertTrue((ROOT/'firmware/stm32'/relative).is_file(),relative)

if __name__ == '__main__':
    unittest.main()
