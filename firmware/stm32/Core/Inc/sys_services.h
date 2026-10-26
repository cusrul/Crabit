#ifndef SYS_SERVICES_H
#define SYS_SERVICES_H

#include "event_bus.h"
#include "global_store.h"
#include "sys_power.h"
#include "rtc.h" // 提供 hrtc 句柄与 RTC 接口
#include "badusb_armory.h"
#include "companion_ble.h"
#include "network_weather.h"
class SysServices {
public:
    static void init(WindowManager* wm) {
        WindowManager::instance = wm;
        Crabit::NetworkWeather::init();

        // 1. 系统通知订阅
        EventBus::subscribe("sys/notify", [](const char* payload) {
            strncpy(GlobalStore.latest_notify, payload, sizeof(GlobalStore.latest_notify)-1);
            GlobalStore.latest_notify[63] = '\0';
            SysPower::feed(); 
        });

        // 2. 蓝牙状态订阅
        EventBus::subscribe("sys/ble", [](const char* payload) {
            Crabit::CompanionBle::observeModeStatus(payload);
            strncpy(GlobalStore.ble_status, payload, sizeof(GlobalStore.ble_status)-1);
        });

        // 3. 网络时间同步：写入 RTC 并更新界面时间
        EventBus::subscribe("sys/time", [](const char* payload) {
            // payload 格式: "2026-03-22 12:30:05"
            int year, month, day, hour, min, sec;
            
            // 按固定格式解析时间字符串
            if (sscanf(payload, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) == 6) {
                
                RTC_TimeTypeDef sTime = {0};
                RTC_DateTypeDef sDate = {0};

                // 写入时间
                sTime.Hours = (uint8_t)hour;
                sTime.Minutes = (uint8_t)min;
                sTime.Seconds = (uint8_t)sec;
                sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
                sTime.StoreOperation = RTC_STOREOPERATION_RESET;
                HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

                // 写入日期 (STM32 RTC 的 Year 是 0-99，所以需要 % 100)
                sDate.Year = (uint8_t)(year % 100);
                sDate.Month = (uint8_t)month;
                sDate.Date = (uint8_t)day;
                // 星期可以先简单设为 1，RTC 会自己算，或者保持原样
                sDate.WeekDay = RTC_WEEKDAY_MONDAY; 
                HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

                // 同步完成，更新 UI 状态
                snprintf(GlobalStore.net_time, 16, "%02d:%02d:%02d", hour, min, sec);
                strncpy(GlobalStore.latest_notify, "RTC SYNC OK!", 63);
                
            } else {
            }
            
            SysPower::feed(); 
            if (WindowManager::instance) WindowManager::instance->markDirty();
        });

// 4. BadUSB 指令订阅
        EventBus::subscribe("app/badusb", [](const char* payload) {
            const char* separator = strchr(payload, '|');
            if (separator != nullptr) {
                int exec_time = 0;
                sscanf(payload, "%d", &exec_time); 
                const char* script = separator + 1;
                
                // 写入 Flash 任务列表
                if (BadUsbArmory::storePayload((uint32_t)exec_time, script, 0x01)) {
                    USB_LOG("✅ [ARMORY] Payload Armed! Time: %d\r\n", exec_time);
                    strncpy(GlobalStore.latest_notify, "Payload Armed!", 63);
                } else {
                    USB_LOG("❌ [ARMORY] ERROR: Flash is FULL or Write Failed!\r\n");
                    strncpy(GlobalStore.latest_notify, "Armory FULL!", 63);
                }
            } else {
                if (BadUsbArmory::storePayload(0, payload, 0x01)) {
                    USB_LOG("✅ [ARMORY] Immediate Payload Armed!\r\n"); 
                } else {
                    USB_LOG("❌ [ARMORY] ERROR: Flash is FULL!\r\n");
                }
            }
            
            // SysPower::feed(); 
            if (WindowManager::instance) WindowManager::instance->markDirty();
        });




    }
};

#endif
