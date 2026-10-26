#ifndef APP_BW16_FLASH_H
#define APP_BW16_FLASH_H

#include "NanoUI/NanoUI.h"
#include "event_bus.h" 
#include "global_store.h" 
#include <string.h>
#include <vector> // 馃専 鏄惧紡寮曞叆 vector

#define SYS_USB_MODE 1
#ifndef SYS_USB_MODE
#define SYS_USB_MODE 0
#endif

extern "C" void BW16_Dedicated_Bridge();

// ==================================================
// 馃専 闈欐€佸叏灞€鍙橀噺锛氬畬缇庨伩寮€ Lambda 鐨?[this] 鎹曡幏闄愬埗
// ==================================================
static bool g_ota_started = false;
static char g_ota_status_msg[32] = "就绪";

class FlashApp : public Application {
private:
    char last_cache[64] = {0};

public:
    const char* getAppName() override { return "BW16_UPDATE"; }

    void onCreate() override {
        // 姣忔杩涘叆椤甸潰鏃讹紝閲嶇疆鐘舵€?
        g_ota_started = false;
        strcpy(g_ota_status_msg, "就绪");
    }

    void onDestroy() override {
        if (g_ota_started) {
            EventBus::publish("sys/mode", "REBOOT");
            g_ota_started = false; // 娓呴櫎鏍囪
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
        // 馃専 鏄惧紡鏋勫缓 vector锛屽交搴曟秷鐏?Column 鏋勯€犲嚱鏁扮殑绫诲瀷鍖归厤鎶ラ敊
        // ==================================================
        NanoUI::EmbeddedVector<Widget*> children;
        children.push_back(new Spacer());
        children.push_back(new Text("BW16 固件烧录", 8));
        children.push_back(new SizedBox(0, 5));

#if SYS_USB_MODE == 1                
        children.push_back(new Text("USB 串口桥接 1.5M", 8));
        children.push_back(new SizedBox(0, 6));
        children.push_back(new Button(new DecoratedBox(new Padding(new Text("开始烧录", 8), Insets::all(3)), false, 2), [](){
            BW16_Dedicated_Bridge(); 
        }));
#else
        // 馃専 涓嶅甫鎹曡幏 [] 鐨勭函鍑€ Lambda锛屽畬缇庨€傞厤 Button 鐨勫洖璋冿紒
        children.push_back(new Button(new Text("启动 OTA 热点", 8), [](){
            strcpy(g_ota_status_msg, "Waking up BW16...");
            if (WindowManager::instance) WindowManager::instance->markDirty();
            
            // 1. 纭欢绾ф墦鏂紤鐪?
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
            HAL_Delay(1000); 
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
            HAL_Delay(2000); // 缁欏畠 1.5 绉掕嫃閱?
            
            // 2. 鍙戝竷 OTA 妯″紡鎸囦护
            EventBus::publish("sys/mode", "OTA");
            g_ota_started = true;
            strcpy(g_ota_status_msg, "Sending CMD...");
            if (WindowManager::instance) WindowManager::instance->markDirty();
        }));
#endif
        children.push_back(new Spacer());
        children.push_back(new Text("桥接中按上键退出", 8));

        return new Container(new Column(children), 128, 64, 0, false);
    }
};

#endif

