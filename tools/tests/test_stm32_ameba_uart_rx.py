"""Host regressions for the STM32 AMEBA UART RX recovery boundary."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp"
HAL_UART = ROOT / "firmware/stm32/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart.c"
BOOT = ROOT / "firmware/stm32/Core/NanoUI/app_real/app_coproc.h"
FAKES = ROOT / "tools/tests/ameba_frame_fakes"


def extract_function(text, signature):
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
    raise AssertionError("unterminated production function")


class STM32AmebaUartRxTests(unittest.TestCase):
    def test_extracted_production_helper_with_hal_api_mocks(self):
        production = extract_function(
            PORT.read_text(encoding="utf-8"), 'extern "C" bool STM32_PrepareAmebaUartRx(void)'
        )
        compiler = next(
            (
                p
                for p in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++"))
                if p and Path(p).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "an existing local g++ is required")
        with tempfile.TemporaryDirectory(prefix="stm32-ameba-uart-rx-") as tmp:
            source = Path(tmp) / "helper.cpp"
            binary = Path(tmp) / "helper-host.exe"
            fixture = (FAKES / "helper.cpp").read_text(encoding="utf-8")
            before, after = fixture.split("// PRODUCTION_HELPER_INSERTION_POINT", 1)
            source.write_text(before + "\n" + production + "\n" + after, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c++11", "-Wall", "-Wextra", str(source), "-o", str(binary)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("STM32 AMEBA UART RX helper mock checks passed", run.stdout)

    def test_hal_source_guards_preserve_the_documented_failure_mechanism(self):
        hal = HAL_UART.read_text(encoding="utf-8")
        init = extract_function(hal, "HAL_StatusTypeDef HAL_UART_Init(")
        check_idle = extract_function(hal, "HAL_StatusTypeDef UART_CheckIdleState(")
        irq = extract_function(hal, "void HAL_UART_IRQHandler(")
        dma_complete = extract_function(hal, "static void UART_DMAReceiveCplt(")
        self.assertRegex(init, r"if\s*\(huart->gState\s*==\s*HAL_UART_STATE_RESET\)")
        self.assertIn("HAL_UART_MspInit(huart);", init)
        self.assertIn("huart->ReceptionType = HAL_UART_RECEPTION_STANDARD;", check_idle)
        self.assertIn("huart->ReceptionType == HAL_UART_RECEPTION_TOIDLE", irq)
        self.assertIn("HAL_UARTEx_RxEventCallback", irq)
        self.assertIn("HAL_UART_RxCpltCallback", dma_complete)
        self.assertIn("HAL_UART_RECEPTION_TOIDLE", dma_complete)
        body = extract_function(
            PORT.read_text(encoding="utf-8"), 'extern "C" bool STM32_PrepareAmebaUartRx(void)'
        )
        for forbidden in (
            "MX_USART1_UART_Init",
            "HAL_UART_Init(",
            "HAL_UART_Transmit",
            "HAL_DMA_Abort",
            "HAL_DMA_Stop",
            "HAL_NVIC_ClearPendingIRQ",
            "BW16_NormalBoot",
            "SysPower::",
            "USB_LOG",
            "HAL_NVIC_SetPriority",
            "HAL_UART_MspInit",
            "GPIO_PIN_6",
            "HAL_GPIO_DeInit",
            "AXP2101",
        ):
            self.assertNotIn(forbidden, body)
        self.assertIn("ATOMIC_CLEAR_BIT(huart1.Instance->CR1, USART_CR1_IDLEIE)", body)
        self.assertIn("DMA1_Channel4_IRQn", body)
        self.assertIn("NVIC_GetEnableIRQ(DMA1_Channel4_IRQn)", body)
        self.assertIn("HAL_NVIC_DisableIRQ(USART1_IRQn)", body)
        self.assertIn("HAL_NVIC_DisableIRQ(DMA2_Channel7_IRQn)", body)
        boot = BOOT.read_text(encoding="utf-8")
        normal_boot = extract_function(boot, "inline void BW16_NormalBoot(")
        self.assertIn("GPIO_MODE_INPUT", normal_boot)
        self.assertIn("GPIO_PULLUP", normal_boot)
        self.assertRegex(normal_boot, r"HAL_GPIO_Init\(GPIOB,\s*&GPIO_InitStruct\)")


if __name__ == "__main__":
    unittest.main()
