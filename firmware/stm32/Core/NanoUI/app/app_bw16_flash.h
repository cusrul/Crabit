#ifndef APP_BW16_FLASH_H
#define APP_BW16_FLASH_H

#include "../src/NanoUI.h"
#include "event_bus.h" 
#include "global_store.h" 
#include <string.h>
#include <vector> // 显式引入 vector

#define SYS_USB_MODE 1
#ifndef SYS_USB_MODE
#define SYS_USB_MODE 0
#endif

extern "C" void BW16_Dedicated_Bridge();

// ==================================================
// 静态全局变量：完美避开 Lambda 的 [this] 捕获限制
// ==================================================
static bool g_ota_started = false;
static char g_ota_status_msg[32] = "Ready";

class FlashApp : public Application {
private:
    char last_cache[64] = {0};

public:
    const char* getAppName() override { return "BW16_UPDATE"; }

    void onCreate() override {
        // 每次进入页面时，重置状态
        g_ota_started = false;
        strcpy(g_ota_status_msg, "Ready");
    }

    void onDestroy() override {
        if (g_ota_started) {
            EventBus::publish("sys/mode", "REBOOT");
            g_ota_started = false; // 清除标记
        }
    }

    void onTick() override {
        if (strcmp(last_cache, GlobalStore.latest_notify) != 0) {
            strncpy(last_cache, GlobalStore.latest_notify, 63);
            strncpy(g_ota_status_msg, GlobalStore.latest_notify, 31); 
            if (WindowManager::instance) WindowManager::instance->markDirty();
        }
    }

    Widget* buildUI() override {
        // ==================================================
        // 显式构建 vector，彻底消灭 Column 构造函数的类型匹配报错
        // ==================================================
        std::vector<Widget*> children;
        children.push_back(new Spacer());
        children.push_back(new Text("[ BW16 UPDATE ]", 1, true));
        children.push_back(new SizedBox(0, 2));
        children.push_back(new Text(g_ota_status_msg, 1));
        children.push_back(new SizedBox(0, 4));

#if SYS_USB_MODE == 1                
        children.push_back(new Text("1.5M Baudrate Ready", 1));
        children.push_back(new Button(new Text("START BRIDGE", 1), [](){
            BW16_Dedicated_Bridge(); 
        }));
#else
        // 不带捕获 [] 的纯净 Lambda，完美适配 Button 的回调！
        children.push_back(new Button(new Text("START OTA AP", 1), [](){
            strcpy(g_ota_status_msg, "Waking up BW16...");
            if (WindowManager::instance) WindowManager::instance->markDirty();
            
            // 1. 硬件级打断休眠
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
            HAL_Delay(1000); 
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
            HAL_Delay(2000); // 给它 1.5 秒苏醒
            
            // 2. 发布 OTA 模式指令
            EventBus::publish("sys/mode", "OTA");
            g_ota_started = true;
            strcpy(g_ota_status_msg, "Sending CMD...");
            if (WindowManager::instance) WindowManager::instance->markDirty();
        }));
#endif
        children.push_back(new Spacer());
        children.push_back(new Text("Hold Key to Exit", 1));

        return new Container(new Column(children), 128, 64, 0, true);
    }
};

#endif