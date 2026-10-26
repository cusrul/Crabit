"""Build a Windows interactive review of product ClockApp/SettingsApp, no devices."""

from pathlib import Path
import argparse
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/watch-live-review"
UI = ROOT / "firmware/stm32/Core/NanoUI"


def stage_product_boundaries():
    # Preserve real menu composition/callbacks; replace other board Apps only.
    source = (UI / "app_real/launcher.h").read_text(encoding="utf-8")
    source, count = re.subn(r'^#include "app_[^"\n]+"[^\n]*', "", source, flags=re.M)
    if count < 10 or source.count("class FolderSystemApp") != 1:
        raise ValueError("Product launcher boundary changed; inspect staging before building")
    (OUT / "launcher_preview.h").write_text(source, encoding="utf-8")
    port = (ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp").read_text(encoding="utf-8")
    begin = "class FreeRTOSMemoryAdapter : public NanoUI::MemoryAdapter {"
    end = "static FreeRTOSMemoryAdapter g_rtos_allocator;"
    if port.count(begin) != 1 or port.count(end) != 1:
        raise ValueError("Expected exactly one product UI allocator")
    allocator = port.split(begin, 1)[1].split(end, 1)[0]
    (OUT / "product_ui_pool.h").write_text(
        "#pragma once\n#include <cstdlib>\n"
        "inline void* pvPortMalloc(size_t size) { return std::malloc(size); }\n"
        "#define USB_LOG(...) ((void)0)\n" + begin + allocator,
        encoding="utf-8",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-regression", action="store_true")
    args = parser.parse_args()
    compiler = shutil.which("g++")
    if not compiler:
        raise RuntimeError("Existing host g++ required; no installation attempted")
    OUT.mkdir(parents=True, exist_ok=True)
    stage_product_boundaries()
    executable = OUT / "crabit-watch-review.exe"
    command = [compiler, "-std=c++17", "-O1", "-g", "-mwindows", "-DNANOUI_DYNAMIC_STORAGE=0"]
    for directory in (
        OUT,
        ROOT / "tools/ui_review/live_fakes",
        UI / "app_real",
        UI / "include",
        ROOT / "firmware/stm32/Core/Inc",
        ROOT / "libraries/NanoUI/src",
    ):
        command += ["-I", str(directory)]
    command += [
        str(ROOT / "tools/ui_review/live_preview.cpp"),
        str(UI / "src/font_engine.cpp"),
        str(UI / "src/nanoui_assets.cpp"),
        str(ROOT / "libraries/NanoUI/src/NanoUI/product/runtime.cpp"),
        "-lgdi32",
        "-luser32",
        "-o",
        str(executable),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    subprocess.run([str(executable), "--self-test"], check=True, cwd=OUT, timeout=30)
    print("Product ClockApp/SettingsApp self-test passed (offline services, not hardware).")
    if args.verify_regression:
        # Reintroduce only the redundant wrapper as a negative control; no Git
        # history or SDK dependency, and never edit the product source itself.
        negative = OUT / "negative-wrapper"
        negative.mkdir(exist_ok=True)
        settings = (UI / "app_real/app_settings.h").read_text(encoding="utf-8")
        settings, count = re.subn(
            r"new ConstrainedBox\(new Align\(new Text\(label, 8\), Alignment::CenterLeft, true\),"
            r"\s*BoxConstraints::tight\(60, 18\)\)",
            "new Container(new Align(new Text(label, 8), Alignment::CenterLeft, true), 60, 18)",
            settings,
        )
        if count != 1:
            raise ValueError("Expected one Settings row sizing wrapper for negative control")
        (negative / "app_settings.h").write_text(settings, encoding="utf-8")
        negative_exe = negative / "crabit-watch-negative.exe"
        negative_command = command.copy()
        index = negative_command.index("-I")
        negative_command[index:index] = ["-I", str(negative)]
        negative_command[-1] = str(negative_exe)
        subprocess.run(negative_command, check=True, cwd=ROOT)
        result = subprocess.run([str(negative_exe), "--self-test"], cwd=negative, timeout=30)
        error = (negative / "self-test-error.txt").read_text(encoding="utf-8")
        if result.returncode != 4 or "12KiB UI pool exhausted" not in error:
            raise AssertionError(
                f"Negative control did not expose the expected pool failure: {error}"
            )
        print("Negative wrapper control reproduced pool exhaustion; current page passes.")
    print(executable)


if __name__ == "__main__":
    main()
