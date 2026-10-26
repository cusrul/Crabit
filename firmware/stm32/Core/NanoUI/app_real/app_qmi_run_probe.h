#pragma once
#include "NanoUI/NanoUI.h"
#include "hw_sensors.h"
#include "qmi_run_probe.h"
#include "companion_ble.h"
#include "ameba_session.h"
#include <cstdio>

extern QMI8658_Driver imu_drv;
extern volatile bool g_coproc_active;
extern "C" volatile uint8_t bw16_flash_mode;

class QmiRunProbeApp : public Application {
    struct Bus {
        uint32_t base0 = 0, base1 = 0;
        bool read(uint16_t a, uint8_t r, uint8_t& v) {
            return HAL_I2C_Mem_Read(&hi2c1, a, r, 1, &v, 1, 10) == HAL_OK;
        }
        bool write(uint16_t a, uint8_t r, uint8_t v) {
            return HAL_I2C_Mem_Write(&hi2c1, a, r, 1, &v, 1, 10) == HAL_OK;
        }
        void delay() { HAL_Delay(1); }
        void armMotion() {
            // Both physical inputs retain their existing rising/pulldown setup.
            // Clear old Tap edges, then take the baseline before WoM is enabled.
            const uint32_t primask = __get_PRIMASK();
            __disable_irq();
            __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0 | GPIO_PIN_1);
            NVIC_ClearPendingIRQ(EXTI0_IRQn);
            NVIC_ClearPendingIRQ(EXTI1_IRQn);
            base0 = g_qmi_int2_cnt;
            base1 = g_qmi_int1_cnt;
            __set_PRIMASK(primask);
        }
    } bus;
    QmiRunProbe::Session<Bus> session{bus, 0};
    NanoUI::NanoUIText lines[6] = {"WMR WAIT 3s", "P0:0 P1:0",    "L0 H0 PR0",
                                   "MODE0 PULL0", "B00 M0 R0 N0", "OK: REPEAT"};
    uint32_t created = 0, started = 0, lastPaint = 0;
    bool launched = false, running = false, restored = true;
    uint8_t high = 0, pending = 0, regs[4]{};
    bool configOk = false;

    void sample() {
        high |= uint8_t(GPIOB->IDR & 3u);
        pending |= uint8_t(EXTI->PR1 & 3u);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "P0:%lu P1:%lu",
                 static_cast<unsigned long>(uint32_t(g_qmi_int2_cnt - bus.base0)),
                 static_cast<unsigned long>(uint32_t(g_qmi_int1_cnt - bus.base1)));
        lines[1] = buffer;
        snprintf(buffer, sizeof(buffer), "L%lX H%X PR%X",
                 static_cast<unsigned long>(GPIOB->IDR & 3u), high, pending);
        lines[2] = buffer;
        snprintf(buffer, sizeof(buffer), "MODE%lX PULL%lX",
                 static_cast<unsigned long>(GPIOB->MODER & 15u),
                 static_cast<unsigned long>(GPIOB->PUPDR & 15u));
        lines[3] = buffer;
        const unsigned nvic =
            (NVIC_GetEnableIRQ(EXTI0_IRQn) ? 1u : 0u) | (NVIC_GetEnableIRQ(EXTI1_IRQn) ? 2u : 0u);
        snprintf(buffer, sizeof(buffer), "B%02lX M%lX R%lX N%X",
                 static_cast<unsigned long>(SYSCFG->EXTICR[0] & 255u),
                 static_cast<unsigned long>(EXTI->IMR1 & 3u),
                 static_cast<unsigned long>(EXTI->RTSR1 & 3u), nvic);
        lines[4] = buffer;
    }
    void finish(bool prepared) {
        // No STATUS1 reads during the interval. Freeze pins/counts BEFORE clear.
        sample();
        uint8_t status = 0;
        const bool statusOk = prepared && bus.read(imu_drv.busAddress(), 0x2F, status);
        restored = session.restore();
        if (!restored)
            SensorStop2::restoreFault.store(true, std::memory_order_relaxed);
        running = false;
        QmiRunProbe::active.store(false, std::memory_order_relaxed);
        char buffer[32];
        if (!prepared)
            snprintf(buffer, sizeof(buffer), "WMR CONFIG FAIL");
        else if (!restored)
            snprintf(buffer, sizeof(buffer), "WMR RESTORE FAIL");
        else if (!statusOk)
            snprintf(buffer, sizeof(buffer), "WMR STATUS FAIL");
        else
            snprintf(buffer, sizeof(buffer), "WMR DONE S%02X", status);
        lines[0] = buffer;
    }
    void start() {
        if (running || !restored)
            return;
        launched = true;
        if (g_coproc_active || bw16_flash_mode || Crabit::amebaSessionActive() ||
            Crabit::CompanionBle::hardwareActive() ||
            Crabit::CompanionBle::hardwareTransitioning() ||
            SensorStop2::restoreFault.load(std::memory_order_relaxed) ||
            I2cDiag::paused.load(std::memory_order_relaxed)) {
            lines[0] = "WMR BLOCKED";
            return;
        }
        QmiRunProbe::active.store(true, std::memory_order_relaxed);
        // Reconstruct the session after driver initialization; never cache address 0.
        session.reset(imu_drv.busAddress());
        high = pending = 0;
        const bool ok = session.prepare();
        configOk = ok;
        constexpr uint8_t addresses[] = {2, 3, 8, 9};
        for (unsigned i = 0; ok && i < 4; ++i)
            configOk = bus.read(imu_drv.busAddress(), addresses[i], regs[i]) && configOk;
        char buffer[32];
        if (configOk)
            snprintf(buffer, sizeof(buffer), "%02X/%02X/%02X/%02X", regs[0], regs[1], regs[2],
                     regs[3]);
        else
            snprintf(buffer, sizeof(buffer), "REG READ FAIL");
        lines[5] = buffer;
        if (!ok) {
            finish(false);
            return;
        }
        started = HAL_GetTick();
        running = true;
        sample();
    }

   public:
    const char* getAppName() override { return "WOM_RUN_PROBE"; }
    void onCreate() override { created = HAL_GetTick(); }
    void onTick() override {
        if (!launched && uint32_t(HAL_GetTick() - created) >= 3000u)
            start();
        // prepare() performs bounded I2C calls; take time AFTER it returns.
        const uint32_t now = HAL_GetTick();
        if (running) {
            sample();
            if (uint32_t(now - started) >= 20000u)
                finish(true);
            else {
                char buffer[32];
                snprintf(
                    buffer, sizeof(buffer), "WMR RUN %lus",
                    static_cast<unsigned long>((20000u - uint32_t(now - started) + 999u) / 1000u));
                lines[0] = buffer;
            }
        }
        if (uint32_t(now - lastPaint) >= 100u) {
            lastPaint = now;
            if (WindowManager::instance)
                WindowManager::instance->markDirty();
        }
    }
    void onDestroy() override {
        if (running)
            finish(true);
    }
    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> rows;
        for (unsigned i = 0; i < 5; ++i)
            rows.push_back(new Container(new SlidingText(&lines[i], 8), 128, 10));
        rows.push_back(new Button(new Container(new SlidingText(&lines[5], 8), 128, 10),
                                  [this]() { start(); }));
        return new Container(new Column(rows), 128, 64);
    }
};
