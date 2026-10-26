#pragma once
#include "mcu_stop2_sequence.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tim.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "usb_device.h"
#include "usbd_core.h"
#include "usb_keyboard.h"
#include "axp2101.h"
#include "ameba_session.h"
#include "companion_ble.h"
#include "event_bus.h"
#include "i2c_sample_trace.h"
#include "hw_sensors.h"
#include "sensor_stop2.h"
#include "led_rail_stop2.h"
#include "sys_config.h"
#include "input_feedback.h"
#include "qmi_wake_trace.h"

extern QMI8658_Driver imu_drv;
extern PAT9125_Driver pat_drv;
extern BMP280_Driver bmp_drv;
extern SystemConfig sys_cfg;

extern "C" volatile uint8_t bw16_flash_mode;
extern volatile bool g_coproc_active;
extern USBD_HandleTypeDef hUsbDeviceFS;
extern TIM_HandleTypeDef htim1;
extern "C" void SystemClock_Config(void);
extern "C" void STM32_RestartUartRx(void);
extern void OLED_SetSleep(bool sleep);

namespace McuStop2 {
class WatchPort {
    struct LedRailPort {
        uint32_t mode = 0, pull = 0, type = 0, speed = 0, af = 0, output = 0;
        bool readEnable(uint8_t& value) {
            return AXP2101_ReadReg(&hi2c1, AXP2101_LDO_ONOFF_CTRL0, &value) == HAL_OK;
        }
        bool writeEnable(uint8_t value) {
            return AXP2101_WriteReg(&hi2c1, AXP2101_LDO_ONOFF_CTRL0, value) == HAL_OK;
        }
        void holdDataLow() {
            mode = GPIOA->MODER;
            pull = GPIOA->PUPDR;
            type = GPIOA->OTYPER;
            speed = GPIOA->OSPEEDR;
            af = GPIOA->AFR[0];
            output = GPIOA->ODR;
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
            GPIO_InitTypeDef pin = {};
            pin.Pin = GPIO_PIN_5;
            pin.Mode = GPIO_MODE_OUTPUT_PP;
            pin.Pull = GPIO_NOPULL;
            pin.Speed = GPIO_SPEED_FREQ_LOW;
            HAL_GPIO_Init(GPIOA, &pin);
        }
        void settle() { HAL_Delay(5); }  // Initial test margin, not physical rail verification.
        void restoreDataPin() {
            constexpr uint32_t two = 3u << 10u, four = 15u << 20u;
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5,
                              (output & GPIO_PIN_5) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            MODIFY_REG(GPIOA->PUPDR, two, pull & two);
            MODIFY_REG(GPIOA->OTYPER, GPIO_PIN_5, type & GPIO_PIN_5);
            MODIFY_REG(GPIOA->OSPEEDR, two, speed & two);
            MODIFY_REG(GPIOA->AFR[0], four, af & four);
            MODIFY_REG(GPIOA->MODER, two, mode & two);
        }
    } ledRailPort_;
    LedRailStop2::Session<LedRailPort> ledRail_{ledRailPort_};
    struct SensorBus {
        bool read(uint16_t addr, uint8_t reg, uint8_t& value) {
            return HAL_I2C_Mem_Read(&hi2c1, addr, reg, 1, &value, 1, 10) == HAL_OK;
        }
        bool write(uint16_t addr, uint8_t reg, uint8_t value) {
            return HAL_I2C_Mem_Write(&hi2c1, addr, reg, 1, &value, 1, 10) == HAL_OK;
        }
        void delay() { HAL_Delay(1); }
        void armMotion() {
            __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);
            NVIC_ClearPendingIRQ(EXTI0_IRQn);
        }
    } sensorBus_;
    SensorStop2::Session<SensorBus> sensors_{sensorBus_, imu_drv.busAddress(),
                                             pat_drv.busAddress()};
    AuxStop2::Session<SensorBus> aux_{sensorBus_, bmp_drv.busAddress(), AXP2101_ADDR};
    bool motion_ = false;
    uint32_t enabled_[8]{}, banks_ = 0, tickCtrl_ = 0, tickLoad_ = 0, pending_ = 0;
    uint32_t pinMode_ = 0, pinPull_ = 0, exticr_ = 0, priority_ = 0;
    uint32_t imr_ = 0, emr_ = 0, rising_ = 0, falling_ = 0;
    uint32_t stopClock_ = 0, debug_ = 0, tim2_ = 0, tim15_ = 0;
    static uint32_t rtcSeconds() {
        const volatile uint32_t ssr = RTC->SSR;
        const uint32_t tr = RTC->TR;
        const volatile uint32_t dr = RTC->DR;
        (void)ssr;
        (void)dr;
        return (((tr >> 20) & 3u) * 10u + ((tr >> 16) & 15u)) * 3600u +
               (((tr >> 12) & 7u) * 10u + ((tr >> 8) & 15u)) * 60u + ((tr >> 4) & 7u) * 10u +
               (tr & 15u);
    }
    Reason checks() {
        if (SensorStop2::restoreFault.load(std::memory_order_relaxed))
            return Reason::SensorFailed;
        if (g_coproc_active || Crabit::amebaSessionActive() || Crabit::CompanionBle::active() ||
            Crabit::CompanionBle::transitioning() || Crabit::CompanionBle::hardwareActive() ||
            Crabit::CompanionBle::hardwareTransitioning() || bw16_flash_mode)
            return Reason::Radio;
        if (!buttonsReleased())
            return Reason::Keys;
        if (I2cDiag::paused.load(std::memory_order_relaxed) ||
            HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY)
            return Reason::I2cFault;
        if (huart1.gState != HAL_UART_STATE_READY ||
            HAL_SPI_GetState(&hspi3) != HAL_SPI_STATE_READY ||
            (EventBus::tx_queue && uxQueueMessagesWaiting(EventBus::tx_queue)) ||
            TIM15->CCR2 != 0 || InputFeedback::manualActive() || (TIM2->CCER & TIM_CCER_CC1E) ||
            (TIM2->DIER & TIM_DIER_CC1DE))
            return Reason::Busy;
        uint8_t status = 0;
        if (AXP2101_ReadReg(&hi2c1, AXP2101_STATUS1, &status) != HAL_OK)
            return Reason::I2cFault;
        if ((status & 0x20u) || hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED ||
            hUsbDeviceFS.dev_state == USBD_STATE_ADDRESSED || CrabitUsb_KeyboardBusy())
            return Reason::Usb;
        return Reason::None;
    }

   public:
    bool prepareSensors() {
        // Existing lift-wake setting selects WoM; no Flash setting migration.
        motion_ = sys_cfg.enable_lift_wake;
        const uint32_t attempt = QmiWakeTrace::last.attempts + 1;
        QmiWakeTrace::last = {};
        QmiWakeTrace::last.attempts = attempt;
        QmiWakeTrace::last.motion = motion_;
        const bool sensorOk = sensors_.prepare(motion_);
        QmiWakeTrace::last.prepared = sensorOk;
        if (sensorOk)
            QmiWakeTrace::captureConfig(sensorBus_, imu_drv.busAddress());
        return sensorOk && ledRail_.prepare() && aux_.prepare();
    }
    bool restoreSensors() {
        // Capture the read-to-clear WoM flag before restore() disables WoM.
        QmiWakeTrace::captureStatus(sensorBus_, imu_drv.busAddress());
        const bool auxOk = aux_.restore();
        if (!auxOk)
            AuxStop2::restoreFault.store(true, std::memory_order_relaxed);
        const bool railOk = ledRail_.restore();
        if (!railOk)
            LedRailStop2::restoreFault.store(true, std::memory_order_relaxed);
        const bool ok = sensors_.restore() && railOk && auxOk;
        if (!ok) {
            SensorStop2::restoreFault.store(true, std::memory_order_relaxed);
            I2cDiag::paused.store(true, std::memory_order_relaxed);
        }
        return ok;
    }
    bool motionPending() const {
        return motion_ &&
               ((EXTI->PR1 & GPIO_PIN_0) || HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_SET);
    }
    bool buttonsReleased() const {
        return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_SET &&
               HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_SET &&
               HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_RESET;
    }
    Reason blocked() {
        if (__get_IPSR() || __get_PRIMASK() || __get_BASEPRI())
            return Reason::Busy;
        return checks();
    }
    Reason blockedWhilePaused() { return checks(); }
    void displaySleep() { OLED_SetSleep(true); }
    void displayWake() { OLED_SetSleep(false); }
    void pause() {
        vTaskSuspendAll();
        __disable_irq();
        tickCtrl_ = SysTick->CTRL;
        tickLoad_ = SysTick->LOAD;
        pending_ = SCB->ICSR & (SCB_ICSR_PENDSTSET_Msk | SCB_ICSR_PENDSVSET_Msk);
        SysTick->CTRL = tickCtrl_ & ~(SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk);
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
        banks_ = (SCnSCB->ICTR & 15u) + 1u;
        if (banks_ > 8)
            banks_ = 8;
        for (uint32_t i = 0; i < banks_; ++i) {
            enabled_[i] = NVIC->ISER[i];
            NVIC->ICER[i] = 0xffffffffu;
        }
        pinMode_ = GPIOA->MODER;
        pinPull_ = GPIOA->PUPDR;
        exticr_ = SYSCFG->EXTICR[1];
        priority_ = NVIC_GetPriority(EXTI9_5_IRQn);
        imr_ = EXTI->IMR1;
        emr_ = EXTI->EMR1;
        rising_ = EXTI->RTSR1;
        falling_ = EXTI->FTSR1;
        stopClock_ = RCC->CFGR & RCC_CFGR_STOPWUCK;
        debug_ = DBGMCU->CR;
        tim2_ = TIM2->CR1;
        tim15_ = TIM15->CR1;
        // Keep the HAL timebase alive for bounded HAL calls, but no scheduling/USB/UART IRQs.
        NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);
        __enable_irq();
    }
    bool quiesce() {
        bool ok = USBD_Stop(&hUsbDeviceFS) == USBD_OK;
        ok = (USBD_DeInit(&hUsbDeviceFS) == USBD_OK) && ok;
        HAL_PWREx_DisableVddUSB();
        ok = (HAL_UART_DeInit(&huart1) == HAL_OK) && ok;
        ok = (HAL_SPI_DeInit(&hspi3) == HAL_OK) && ok;
        ok = (HAL_I2C_DeInit(&hi2c1) == HAL_OK) && ok;
        // Only ALDO2 was gated, before disabling I2C. No other PMIC changes.
        return ok;
    }
    void armAndStopTicks() {
        __disable_irq();
        HAL_SuspendTick();
        TIM1->CR1 &= ~TIM_CR1_CEN;
        TIM2->CR1 &= ~TIM_CR1_CEN;
        TIM15->CR1 &= ~TIM_CR1_CEN;
        NVIC_DisableIRQ(TIM1_UP_TIM16_IRQn);
        GPIO_InitTypeDef pin = {};
        pin.Pin = GPIO_PIN_6;
        pin.Mode = GPIO_MODE_IT_FALLING;
        pin.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOA, &pin);
        EXTI->IMR1 = GPIO_PIN_6;
        EXTI->EMR1 = 0;
        if (motion_) {
            // Awake WoM probe measured PB0 edges (P0:14, P1:0).
            // Keep QMI INT1 output; listen on physical PB0/EXTI0.
            // Do not clear a real WoM edge that arrived during quiesce.
            EXTI->RTSR1 |= GPIO_PIN_0;
            EXTI->FTSR1 &= ~GPIO_PIN_0;
            EXTI->IMR1 |= GPIO_PIN_0;
            NVIC_ClearPendingIRQ(EXTI0_IRQn);
            NVIC_EnableIRQ(EXTI0_IRQn);
        }
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_6);
        NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
        NVIC_SetPriority(EXTI9_5_IRQn, 5);
        NVIC_EnableIRQ(EXTI9_5_IRQn);
        DBGMCU->CR &= ~DBGMCU_CR_DBG_STOP;
        __HAL_RCC_WAKEUPSTOP_CLK_CONFIG(RCC_STOP_WAKEUPCLOCK_HSI);
        auto& trace = QmiWakeTrace::last;
        trace.armed = true;
        trace.beforeLevels = uint8_t(GPIOB->IDR & 3u);
        trace.beforePending = uint8_t(EXTI->PR1 & 3u);
        trace.mask = uint8_t(EXTI->IMR1 & 3u);
        trace.map = uint8_t(SYSCFG->EXTICR[0] & 15u);  // Selected EXTI0 port.
    }
    Result wait() {
        Result result;
        const uint32_t before = rtcSeconds();
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
        __DSB();
        HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
        __ISB();
        result.waited = true;
        result.reason = (EXTI->PR1 & GPIO_PIN_6)              ? Reason::Pa6
                        : motion_ && (EXTI->PR1 & GPIO_PIN_0) ? Reason::Qmi
                                                              : Reason::Other;
        result.seconds = (rtcSeconds() + 86400u - before) % 86400u;
        return result;
    }
    void restoreClock(Result& result) {
        auto& trace = QmiWakeTrace::last;
        trace.returned = result.waited;
        trace.afterLevels = uint8_t(GPIOB->IDR & 3u);
        trace.afterPending = uint8_t(EXTI->PR1 & 3u);
        // PRIMASK still set: no ordinary ISR/task is allowed to run on HSI16.
        NVIC_DisableIRQ(EXTI9_5_IRQn);
        NVIC_DisableIRQ(EXTI0_IRQn);
        SystemCoreClockUpdate();
        if (HAL_InitTick(uwTickPrio) != HAL_OK)
            Error_Handler();
        __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
        NVIC_ClearPendingIRQ(TIM1_UP_TIM16_IRQn);
        NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);
        __enable_irq();
        SystemClock_Config();
        result.hclk = HAL_RCC_GetSysClockFreq() >>
                      AHBPrescTable[(RCC->CFGR & RCC_CFGR_HPRE) >> RCC_CFGR_HPRE_Pos];
    }
    void restorePeripherals() {
        MX_I2C1_Init();
        MX_SPI3_Init();
        MX_USART1_UART_Init();
        STM32_RestartUartRx();
        MX_USB_DEVICE_Init();  // MCU clock restored first; sensor rails retained.
    }
    void resume() {
        __disable_irq();
        MODIFY_REG(RCC->CFGR, RCC_CFGR_STOPWUCK, stopClock_);
        DBGMCU->CR = debug_;
        constexpr uint32_t pinMask = 3u << (6u * 2u);
        MODIFY_REG(GPIOA->MODER, pinMask, pinMode_ & pinMask);
        MODIFY_REG(GPIOA->PUPDR, pinMask, pinPull_ & pinMask);
        MODIFY_REG(SYSCFG->EXTICR[1], 15u << 8u, exticr_ & (15u << 8u));
        EXTI->IMR1 = imr_;
        EXTI->EMR1 = emr_;
        EXTI->RTSR1 = rising_;
        EXTI->FTSR1 = falling_;
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_8 | GPIO_PIN_14);
        NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
        NVIC_SetPriority(EXTI9_5_IRQn, priority_);
        TIM2->CR1 = tim2_;
        TIM15->CR1 = tim15_;
        for (uint32_t i = 0; i < banks_; ++i) {
            NVIC->ICER[i] = 0xffffffffu;
            NVIC->ISER[i] = enabled_[i];
        }
        SysTick->LOAD = tickLoad_;
        SysTick->VAL = 0;
        SysTick->CTRL = tickCtrl_;
        SCB->ICSR = pending_;
        __enable_irq();
        xTaskResumeAll();
    }
};
inline Result runWatch() {
    WatchPort port;
    return run(port);
}
}  // namespace McuStop2
