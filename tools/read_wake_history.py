"""Read the retained STOP2 wake trace over the existing STM32 USB CDC port.

Usage: python tools/read_wake_history.py COM7
Read-only: never resets the target, clears history, or enters flash mode.
"""
import argparse
import re
import sys
import time

import serial

ROW = re.compile(r"^CRABIT:WAKE ([0-9A-F]{8}) s=(\d+) t=(\d+) e=(\d+) a=([0-9A-F]{8}) b=([0-9A-F]{8}) c=([0-9A-F]{8}) d=([0-9A-F]{8})$")
REASONS = ("none", "usb", "radio", "keys", "busy", "i2c", "pa6", "other", "init_failed", "qmi", "sensor_failed", "motion_pending")
CAUSES = ("none", "viewing_pose", "missing_or_stale_data", "movement", "no_motion")


def signed(hex_value):
    value = int(hex_value, 16)
    return value - (1 << 32) if value & 0x80000000 else value


def explain(match):
    _, seq, tick, event, a, b, c, d = match.groups()
    event, av, bv, cv, dv = int(event), *(int(v, 16) for v in (a, b, c, d))
    prefix = f"#{seq} tick={tick} "
    if event == 1:
        return prefix + "SCREEN_OFF"
    if event == 2:
        reason = av & 0xff
        name = REASONS[reason] if reason < len(REASONS) else f"unknown_{reason}"
        trace = bool(av & 0x200)
        status = f"{dv & 255:02X}" if av & 0x400 else "--"
        return (prefix + f"STOP reason={name} wfi={int(bool(av & 0x100))} seconds={bv} "
                f"trace={int(trace)} prepared={int(bool(av & 0x800))} "
                f"before=L{cv & 15:X}/P{(cv >> 4) & 15:X} "
                f"after=L{(cv >> 8) & 15:X}/P{(cv >> 12) & 15:X} "
                f"mask={(cv >> 16) & 15:X} map={(cv >> 20) & 15:X} "
                f"status={status} wom={(dv >> 8) & 255:02X}/{(dv >> 16) & 255:02X} valid={(dv >> 24) & 255:02X}")
    if event == 3:
        cause = av & 255
        name = CAUSES[cause] if cause < len(CAUSES) else f"unknown_{cause}"
        axes = [signed(value) for value in (b, c, d)]
        return (prefix + f"CONFIRM {name} fresh={int(bool(av & 0x100))} "
                f"zone={int(bool(av & 0x200))} samples={av >> 16} "
                f"axes_mg={axes[0]},{axes[1]},{axes[2]}")
    if event == 4:
        return prefix + "PA6_DURING_CONFIRM"
    if event == 5:
        return prefix + f"FAIL_OPEN count={av}"
    if event in (6, 7):
        return prefix + f"{'BEFORE' if event == 6 else 'AFTER'} CORE ICSR={a} SHCSR={b} PRIMASK={c} SYSTICK_VAL={d}"
    if event in (8, 9, 10, 11):
        label = {8: 'BEFORE NVIC_PENDING', 9: 'AFTER NVIC_PENDING', 10: 'BEFORE NVIC_ENABLED', 11: 'AFTER NVIC_ENABLED'}[event]
        return prefix + f"{label} banks={a}/{b}/{c}"
    if event in (12, 13):
        return prefix + f"{'BEFORE' if event == 12 else 'AFTER'} EXTI_PR={a}/{b} PWR_SR={c}/{d}"
    return prefix + f"event={event} a={a} b={b} c={c} d={d}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="STM32 USB CDC port, e.g. COM7")
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.2, write_timeout=1) as device:
        print("Wake history: RAM only; tick stops during STOP2. Do not reset the watch.")
        for index in range(49):
            token = f"{index:08X}"
            device.write(f"CRABIT:WAKE:GET {token}\n".encode("ascii"))
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                line = device.readline().decode("ascii", "replace").strip()
                if not line.startswith(f"CRABIT:WAKE {token} "):
                    continue
                print(line)
                if " END " in line:
                    return 0
                match = ROW.fullmatch(line)
                if not match:
                    raise ValueError("Malformed wake history response")
                print("  " + explain(match))
                break
            else:
                raise TimeoutError(f"No response for record {index}; verify firmware and USB CDC port")
    return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (serial.SerialException, TimeoutError, ValueError) as exc:
        print(f"Wake history read failed: {exc}", file=sys.stderr)
        sys.exit(1)
