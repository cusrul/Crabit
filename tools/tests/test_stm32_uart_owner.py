"""Host checks for the explicit USART1/CDC owner gate.

The production helper block and UartOwner_* bodies are extracted from
uart_task.cpp and executed against HAL/FreeRTOS fakes. Source guards pin the
bridge and CDC call sites that cannot be host-compiled in this fixture.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

def function(text, signature):
    """Return the last matching production body (declarations come first)."""
    start = text.rindex(signature)
    brace = text.index("{", start)
    depth = 0
    for end in range(brace, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return text[start : end + 1]
    raise AssertionError(f"unterminated production function: {signature}")


ROOT = Path(__file__).resolve().parents[2]
UART = ROOT / "firmware/stm32/Core/Src/uart_task.cpp"
BRIDGE = ROOT / "firmware/stm32/Core/Src/app_bw16_flash.cpp"
BYPASS = ROOT / "firmware/stm32/Core/NanoUI/app_real/app_bus_release.h"
CDC = ROOT / "firmware/stm32/USB_DEVICE/App/usbd_cdc_if.c"
USB = ROOT / "firmware/stm32/Core/Src/usb.cpp"
HARNESS = Path(__file__).with_name("uart_owner_fakes") / "harness.cpp"
MARKER = "// PRODUCTION_INSERTION_POINT"

EXTRACTED = (
    "bool UartOwner_Claim(UartOwnerMode mode)",
    "bool UartOwner_Release(UartOwnerMode mode)",
    "UartOwnerMode UartOwner_Active(void)",
    "bool UartOwner_Normal(void)",
    "bool UartOwner_RequestLineCoding(uint32_t baudrate)",
    "bool UartOwner_TakePendingLineCoding(uint32_t* baudrate)",
    "uint32_t UartOwner_LineCodingBaud(void)",
    "void UartOwner_SetLineCodingBaud(uint32_t baudrate)",
)


def compiler():
    for candidate in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++")):
        if candidate and Path(candidate).is_file():
            return candidate
    return None


class UartOwnerHostTests(unittest.TestCase):
    def test_production_owner_gate_behaviour(self):
        cxx = compiler()
        self.assertIsNotNone(cxx, "an existing local g++ is required")
        text = UART.read_text(encoding="utf-8")
        start = text.index("constexpr uint32_t kProductBaud")
        end = text.index("}  // namespace", start)
        helpers = text[start:end]
        production = helpers + "\n" + "\n\n".join(function(text, sig) for sig in EXTRACTED)
        helper_text = HARNESS.read_text(encoding="utf-8")
        before, after = helper_text.split(MARKER, 1)
        source_text = before + production + after
        with tempfile.TemporaryDirectory(prefix="crabit-uart-owner-") as tmp:
            source = Path(tmp) / "owner.cpp"
            binary = Path(tmp) / "owner-host.exe"
            source.write_text(source_text, encoding="utf-8")
            built = subprocess.run(
                [
                    cxx,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-I",
                    str(ROOT / "firmware/stm32/Core/Inc"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("UART owner gate host checks passed", run.stdout)

    def test_callbacks_and_tx_worker_respect_the_gate(self):
        text = UART.read_text(encoding="utf-8")
        for signature in (
            "void HAL_UARTEx_RxEventCallback(",
            "void HAL_UART_ErrorCallback(",
        ):
            body = function(text, signature)
            self.assertIn("UartOwner_Normal()", body)
            self.assertIn("STM32_RestartUartRx", body)
        tx = function(text, "void Task_TX_Worker(")
        self.assertIn("taskENTER_CRITICAL()", tx)
        self.assertIn("UartOwner_Normal()", tx)
        for forbidden in ("HAL_UART_Transmit_DMA",):
            self.assertEqual(tx.count(forbidden), 1)
        modem = function(text, "void Task_Modem(")
        self.assertIn("if (!UartOwner_Normal()) continue;", modem)
        send = function(text, "bool Uart::send(")
        self.assertIn("UartOwner_Normal()", send)

    def test_bridge_claims_before_reconfig_and_releases_before_normal(self):
        bridge = BRIDGE.read_text(encoding="utf-8")
        body = function(bridge, "bool BW16_Dedicated_Bridge()")
        claim = body.index("UartOwner_Claim(UART_OWNER_FLASH)")
        self.assertLess(claim, body.index("BW16_Release_Bus();"))
        self.assertLess(claim, body.index("AXP2101_SetDC3Enable(true);"))
        self.assertNotIn("Usb::resumeReceive()", body)
        early_fail = body[body.index("if (huart1.gState != HAL_UART_STATE_RESET)"):body.index("HAL_Delay(200)")]
        self.assertIn("if (Usb::rxReady()) Usb::consumeRx();", early_fail)
        self.assertIn("UartOwner_TakePendingLineCoding", body)
        pending = body.index("UartOwner_TakePendingLineCoding")
        usb_rx = body.index("HAL_UART_Transmit(")
        self.assertLess(pending, usb_rx)  # apply host line coding first
        release = body.index("UartOwner_Release(UART_OWNER_FLASH)")
        self.assertLess(body.index("BW16_Bridge_PowerDown();"), release)
        self.assertIn("if (!restored)", body)
        self.assertNotIn("BW16_Restore_Bus();", body)

        bypass = BYPASS.read_text(encoding="utf-8")
        self.assertIn("BW16_Enter_Bypass()", bypass)
        self.assertIn("BW16_Exit_Bypass()", bypass)
        entry = function(bridge, "bool BW16_Enter_Bypass()")
        self.assertLess(entry.index("UartOwner_Claim(UART_OWNER_BYPASS)"),
                        entry.index("BW16_Release_Bus();"))
        exit_body = function(bridge, "bool BW16_Exit_Bypass()")
        self.assertIn("UartOwner_Release(UART_OWNER_BYPASS)", exit_body)

    def test_flash_bridge_owns_the_cdc_writer_gate(self):
        bridge = BRIDGE.read_text(encoding="utf-8")
        body = function(bridge, "bool BW16_Dedicated_Bridge()")
        self.assertIn("Usb::transmitBridge(", body)
        self.assertNotIn("Usb::transmit(", body)

        usb = USB.read_text(encoding="utf-8")
        transmit = function(usb, "bool Usb::transmit(")
        self.assertIn("UART_OWNER_FLASH", transmit)
        self.assertLess(transmit.index("UART_OWNER_FLASH"),
                        transmit.index("CDC_Transmit_FS"))
        raw = function(usb, "bool Usb::transmitBridge(")
        self.assertIn("CDC_TransmitBridge_FS", raw)
        send = function(CDC.read_text(encoding="utf-8"), "static uint8_t CDC_TransmitOwned(")
        self.assertLess(send.index("__disable_irq()"), send.index("UartOwner_Active()"))
        self.assertLess(send.index("UartOwner_Active()"), send.index("USBD_CDC_TransmitPacket("))
        header = (ROOT / "firmware/stm32/Core/Inc/usb.h").read_text(encoding="utf-8")
        self.assertIn("static bool transmitBridge(", header)

    def test_cdc_isr_only_records_line_coding(self):
        cdc = CDC.read_text(encoding="utf-8")
        set_coding = function(cdc, "static int8_t CDC_Control_FS(")
        self.assertIn("UartOwner_RequestLineCoding(baudrate)", set_coding)
        self.assertIn("UartOwner_LineCodingBaud()", set_coding)
        for forbidden in (
            "HAL_UART_DeInit",
            "HAL_UART_Init",
            "HAL_DMA_Init",
            "HAL_UART_Receive_DMA",
            "hdma_usart1_rx",
            "ring_last_head",
        ):
            self.assertNotIn(forbidden, set_coding)
        receive = function(cdc, "static int8_t CDC_Receive_FS(")
        self.assertIn("UART_OWNER_FLASH", receive)
        self.assertIn("usb_rx_ready = 1;", receive)
        # The raw path must not re-arm before the owner consumes the packet.
        raw = receive[
            receive.index("UART_OWNER_FLASH") : receive.index("CrabitUsb_DiagReceive")
        ]
        self.assertNotIn("USBD_CDC_ReceivePacket(&hUsbDeviceFS);", raw)

        log = function(USB.read_text(encoding="utf-8"), "void Usb::log(")
        self.assertIn("UART_OWNER_FLASH", log)
        self.assertLess(log.index("UART_OWNER_FLASH"),
                        log.index("CDC_Transmit_FS"))


if __name__ == "__main__":
    unittest.main()
