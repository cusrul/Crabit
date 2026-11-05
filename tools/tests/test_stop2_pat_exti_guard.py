"""Source contract for PAT cleanup without dropping QMI or PA6 edges.

Host tests cannot model STM32 NVIC/EXTI latching or establish hardware timing.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2] / "firmware/stm32/Core"


class Stop2PatExtiGuard(unittest.TestCase):
    def test_shared_interrupt_dispatches_pa6_and_pat8(self):
        text = (ROOT / "Src/stm32l4xx_it.c").read_text(encoding="utf-8")
        body = text.split("void EXTI9_5_IRQHandler(void)", 1)[1].split("\n}", 1)[0]
        self.assertIn("HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6)", body)
        self.assertIn("HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8)", body)

    def test_pat_mask_clear_before_nvic_and_pa6_pending_preserved(self):
        text = (ROOT / "Inc/mcu_stop2.h").read_text(encoding="utf-8")
        arm = text.split("void armAndStopTicks()", 1)[1].split("static void captureCore", 1)[0]
        self.assertLess(arm.index("EXTI->IMR1 = GPIO_PIN_6"), arm.index("__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_8)"))
        self.assertLess(arm.index("__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_8)"), arm.index("NVIC_ClearPendingIRQ(EXTI9_5_IRQn)"))
        self.assertNotIn("__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_6)", arm)
        self.assertNotIn("__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0)", arm)
        released = text.split("bool buttonsReleased() const", 1)[1].split("Reason blocked()", 1)[0]
        self.assertIn("!(EXTI->PR1 & GPIO_PIN_6)", released)


if __name__ == "__main__":
    unittest.main()
