#ifndef APP_SYSINFO_H
#define APP_SYSINFO_H

#include "NanoUI/NanoUI.h"
#include <cstdio>
#include "FreeRTOS.h"
#include "main.h"
#include "wake_metrics.h"
#include "wake_diag_format.h"
#include "i2c_sample_trace.h"
#include "sensor_snapshot.h"
#include "usb_device.h"
#include "sys_config.h"
#include "qmi_wake_trace.h"
extern SystemConfig sys_cfg;

extern USBD_HandleTypeDef hUsbDeviceFS;
static_assert(USBD_STATE_DEFAULT == 1 && USBD_STATE_ADDRESSED == 2 && USBD_STATE_CONFIGURED == 3 &&
                  USBD_STATE_SUSPENDED == 4,
              "Diagnostic USB state labels must match the active USB stack");

class SysInfoApp : public Application {
   private:
    // SlidingText keeps a pointer to each source, so these values must outlive the UI tree.
    NanoUI::NanoUIText uptimeText = "00:00:00";
    NanoUI::NanoUIText totalHeapText = "0.0 KB";
    NanoUI::NanoUIText freeHeapText = "0.0 KB";
    NanoUI::NanoUIText minimumFreeHeapText = "0.0 KB";
    NanoUI::NanoUIText usedHeapText = "0.0 KB 0%";
    uint32_t lastUpdate = 0;

    static void formatKilobytes(char* buffer, size_t bufferSize, size_t bytes) {
        snprintf(buffer, bufferSize, "%u.%u KB", static_cast<unsigned>(bytes / 1024U),
                 static_cast<unsigned>((bytes % 1024U) * 10U / 1024U));
    }

    static Widget* infoRow(const char* label, NanoUI::NanoUIText& value) {
        return new Button(
            new Container(
                new Row({new SizedBox(4, 0), new Container(new Text(label, 8), 52, 8), new Spacer(),
                         new Container(new SlidingText(&value, 8), 64, 8), new SizedBox(4, 0)}),
                128, 10),
            []() {});
    }

   public:
    const char* getAppName() override { return "SYSINFO"; }

    void onTick() override {
        const uint32_t now = hal_get_ticks();
        if (now - lastUpdate <= 500)
            return;
        lastUpdate = now;

        char buffer[32];
        const auto& trace = QmiWakeTrace::last;
        snprintf(buffer, sizeof(buffer), "L%X P%X M%X B%X", trace.beforeLevels, trace.beforePending,
                 trace.mask, trace.map);
        uptimeText = buffer;
        if (trace.valid == 0x3F)
            snprintf(buffer, sizeof(buffer), "%02X/%02X/%02X/%02X", trace.regs[0], trace.regs[1],
                     trace.regs[2], trace.regs[3]);
        else
            snprintf(buffer, sizeof(buffer), "READ %02X S%u", trace.valid, trace.prepared);
        totalHeapText = buffer;
        StmSensorSnapshot snapshot{};
        const bool sampled = STM32_ReadSensorSnapshot(&snapshot);
        const uint32_t wake_clock = WakeMetrics::stop_hclk.load(std::memory_order_relaxed);
        WakeDiag::format(
            buffer, sizeof(buffer), WakeDiag::Row::Samples,
            sampled && snapshot.imu.is_valid && uint32_t(now - snapshot.imu.timestamp) <= 200u,
            sampled && snapshot.optical.is_valid &&
                uint32_t(now - snapshot.optical.timestamp) <= 200u,
            wake_clock ? wake_clock : SystemCoreClock);
        freeHeapText = buffer;
        snprintf(buffer, sizeof(buffer), "L%X P%X E%u A%u", trace.afterLevels, trace.afterPending,
                 trace.returned, trace.armed);
        minimumFreeHeapText = buffer;
        if (trace.statusOk)
            snprintf(buffer, sizeof(buffer), "%02X/%02X S%02X", trace.regs[4], trace.regs[5],
                     trace.status);
        else
            snprintf(buffer, sizeof(buffer), "%02X/%02X S--", trace.regs[4], trace.regs[5]);
        usedHeapText = buffer;

        if (WindowManager::instance)
            WindowManager::instance->markDirty();
    }

    Widget* buildUI() override {
        return new Container(
            new Column(
                {new Container(new Row({new SizedBox(4, 0),
                                        new Text(sys_cfg.enable_lift_wake ? "B0 W1" : "B0 W0", 8),
                                        new Spacer()}),
                               128, 12),
                 new Expanded(new ScrollColumn(
                     {infoRow("REG", totalHeapText), infoRow("ARM", uptimeText),
                      infoRow("WAKE", minimumFreeHeapText), infoRow("WOM", usedHeapText),
                      infoRow("QMI/PAT", freeHeapText),
                      new Button(new Container(new Text("返回", 8), 128, 10),
                                 []() { AppManager::exitCurrentApp(); })}))}),
            128, 64);
    }
};

#endif
