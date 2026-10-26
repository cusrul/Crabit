# NanoUI host simulator resources

The host simulator starts the STM32 product `app_real` launcher and uses its
real page classes. It has a separately generated font subset in
`assets/generated/desktop` for the product's 8/12/16/24 px UI. Embedded builds continue to use `src/nanoui_assets.cpp` and
`include/NanoUI/nanoui_assets.h` unchanged.

Run `python generate_resources.py` from here (or its absolute path from any cwd)
after changing desktop text. This calls the common resource packer, reads only
the original product media section and generates fresh desktop fonts; it does
not combine two generated font subsets. Checked-in outputs make normal builds
independent of Python and installed font files.

Regeneration needs Pillow and a TTF font; 8px uses the checked-in Fusion Pixel
BDF with the same alignment as the watch. Other sizes use the configured BDFs
and Windows SimHei fallback. Change the explicit TTF path on another machine. Runtime or
computed strings not present in product sources may need `extra_text`.

The simulator has in-memory platform adapters: no OLED, BLE/Wi-Fi, flash, USB,
PMU, GPIO, or firmware-update driver is linked. It presents the STM32 watch's
current product UI and three-button navigation only; it does not import BW16
game sources. This does not validate physical watch behavior.
