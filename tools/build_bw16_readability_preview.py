"""Build a Windows review window from actual host-rendered NanoUI states only."""

from pathlib import Path
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/bw16-readability-preview"
TESTS = (
    "test_bw16_sniffer_readable_ui",
    "test_bw16_radar_readable_ui",
    "test_bw16_beacon_readable_ui",
    "test_bw16_aux_readable_ui",
)
GROUPS = (
    ("sniffer-readable-ui", "抓包监听"),
    ("bw16-readability-preview/radar", "雷达与频谱"),
    ("bw16-readability-preview/beacon", "信标"),
    ("bw16-readability-preview/aux", "蓝牙、同步、游戏与入口"),
)
LABELS = {
    "mode-0": "选择被动扫描",
    "mode-1": "选择系统扫描",
    "radar-first": "网络列表首项",
    "radar-last": "网络列表末项",
    "menu-first": "任务列表首项",
    "menu-last": "任务列表末项",
    "exec-log-window-0": "运行日志第一页",
    "exec-log-window-1": "运行日志第二页",
    "exec-locked": "固定信道",
    "exec-single-target": "单目标",
    "exec-stop-footer": "停止操作提示",
    "exec-fast-hop-footer": "快速跳频提示",
    "scan": "设备扫描",
    "list-empty": "空设备列表",
    "list-first": "设备列表首项",
    "list-long": "设备列表末项",
    "monitor-safe": "等待目标出现",
    "monitor-blink-black": "接近提醒深色帧",
    "monitor-blink-white": "接近提醒浅色帧",
    "spectrum-empty": "空频谱",
    "spectrum-2g": "2.4G 频谱",
    "spectrum-5g": "5G 频谱",
    "beacon-mode": "扫描方式",
    "beacon-attack": "运行状态",
    "beacon-radar-empty": "空网络列表",
    "beacon-radar-first": "列表首项",
    "beacon-radar-first-network": "长中文网络名与缺字样例",
    "beacon-radar-last": "列表末项",
    "beacon-tactic-first": "测试方式首项",
    "beacon-tactic-last": "测试方式末项",
    "ble-spam-busy": "无线占用提示",
    "ble-spam-running": "蓝牙广播运行中",
    "hopper-title": "游戏开始画面",
    "hopper-over": "游戏结束提示",
    "tool-menu": "原网络工具入口",
    "sync-0": "同步页面：蓝牙开关",
    "sync-1": "同步页面：上传进度",
    "sync-2": "同步页面：清空记录",
}


def main():
    started = time.time()
    subprocess.run(
        [sys.executable, "-m", "unittest", *["tools.tests." + t for t in TESTS], "-v"],
        cwd=ROOT,
        check=True,
    )
    OUTPUT.mkdir(parents=True, exist_ok=True)
    images = OUTPUT / "frames"
    images.mkdir(exist_ok=True)
    entries = []
    for relative, group in GROUPS:
        folder = ROOT / "build" / relative
        # PGM files are the direct 128x64 Canvas output, not enlarged images.
        for file in sorted(folder.glob("*.pgm")):
            if file.stat().st_mtime < started:
                continue  # never display stale probe images
            if file.stem.startswith("state-"):
                continue  # earlier probe artifacts
            target = images / (folder.name + "-" + file.stem + ".bmp")
            with Image.open(file) as image:
                if image.size != (128, 64):
                    raise ValueError(file)
                image.convert("RGB").save(target)
            entries.append((group + " / " + LABELS.get(file.stem, file.stem), target))
    if not entries:
        raise RuntimeError("No real Canvas render outputs")
    entries.sort(key=lambda item: 0 if item[0].endswith("原网络工具入口") else 1)
    (OUTPUT / "manifest.tsv").write_text(
        "".join(f"{label}\t{path}\n" for label, path in entries), encoding="utf-8"
    )
    (OUTPUT / "PREVIEW_ONLY.txt").write_text(
        "Offline simulated UI preview only. No firmware built. Do not flash.\n", encoding="utf-8"
    )
    cxx = shutil.which("g++")
    if not cxx:
        raise RuntimeError("Existing Windows C++ compiler required")
    exe = OUTPUT / "CrabitUiPreview.exe"
    subprocess.run(
        [
            cxx,
            "-std=c++17",
            "-O2",
            "-mwindows",
            str(ROOT / "tools/windows_ui_preview.cpp"),
            "-o",
            str(exe),
            "-lgdi32",
        ],
        check=True,
    )
    # A contact sheet is also reviewable without running the Windows executable.
    font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)
    columns = 3
    cellw = 404
    cellh = 240
    sheet = Image.new(
        "RGB", (columns * cellw, ((len(entries) + columns - 1) // columns) * cellh), "#eeeeee"
    )
    draw = ImageDraw.Draw(sheet)
    for i, (label, path) in enumerate(entries):
        x = i % columns * cellw + 10
        y = i // columns * cellh + 10
        draw.text((x, y), label, font=font, fill="#202020")
        with Image.open(path) as image:
            sheet.paste(image.resize((384, 192), Image.Resampling.NEAREST), (x, y + 30))
    sheet.save(OUTPUT / "contact-sheet.png")
    print(f"{len(entries)} simulated states; Windows preview: {exe}")


if __name__ == "__main__":
    main()
