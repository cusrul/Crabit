#ifndef APP_BLE_KBD_TEST_H
#define APP_BLE_KBD_TEST_H

#include "../src/NanoUI.h"
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEHIDKeyboard.h>

// 实例化官方蓝牙键盘对象
BLEHIDKeyboard testKeyboard;

class AppBleKbdTest : public Application {
private:
    enum AppState { STATE_INIT, STATE_WAIT_CONN, STATE_RUN };
    AppState currentState = STATE_INIT;
    
    char status_msg[32] = "Press OK to type";
    uint32_t last_type_time = 0;

    void drawHeader(Canvas* canvas, const char* title) {
        canvas->fillRect(0, 0, 128, 12, C_WHITE);
        canvas->drawText(2, 2, title, 1, true);
    }

public:
    const char* getAppName() override { return "BLE_KBD_TEST"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        currentState = STATE_INIT;

        while(running && is_display_active) {
            UIEvent e = input->poll();
            
            // 按 BACK (左下角) 退出并切断蓝牙
            if (e.type == EVT_BACK ) {
                running = false;
                break;
            }

            uint32_t now = millis();

            switch(currentState) {
                
                case STATE_INIT: { 
                    canvas->clear();
                    drawHeader(canvas, "KBD TEST INIT");
                    canvas->display();

                    // 1. 清理环境
                    BLE.configAdvert()->stopAdv();
                    BLE.deinit(); 
                    delay(200);

                    // 2. 初始化标准蓝牙键盘
                    BLEAdvertData advdata;
                    advdata.addFlags(); 
                    advdata.addCompleteName("BW16_Keyboard"); 
                    advdata.addAppearance(GAP_GATT_APPEARANCE_HUMAN_INTERFACE_DEVICE); 
                    advdata.addCompleteServices(BLEUUID(HID_SERVICE_UUID));

                    BLEHIDDev.init();
                    BLE.init();

                    BLE.configAdvert()->setAdvData(advdata);
                    BLE.setDeviceName("BW16_Keyboard");
                    BLE.setDeviceAppearance(GAP_GATT_APPEARANCE_HUMAN_INTERFACE_DEVICE);
                    
                    BLE.configSecurity()->setPairable(true);
                    BLE.configSecurity()->setAuthFlags(GAP_AUTHEN_BIT_BONDING_FLAG); 
                    BLE.configServer(3);
                    BLE.addService(BLEHIDDev.hidService());
                    BLE.addService(BLEHIDDev.battService());
                    BLE.addService(BLEHIDDev.devInfoService());

                    BLE.beginPeripheral(); 
                    
                    currentState = STATE_WAIT_CONN;
                    break;
                }

                case STATE_WAIT_CONN:
                    if (BLE.connected()) currentState = STATE_RUN;
                    
                    canvas->clear();
                    drawHeader(canvas, "PAIRING...");
                    canvas->drawText(5, 25, "Connect PC to:");
                    canvas->drawText(5, 40, "BW16_Keyboard");
                    break;

                case STATE_RUN:
                    if (!BLE.connected()) { currentState = STATE_WAIT_CONN; break; }

                    // 核心触发逻辑：按下 OK 键 (左上角)
                    if (e.type == EVT_OK) {
                        last_type_time = now;
                        strcpy(status_msg, ">>> TYPING... >>>");
                        
                        // 调用 Ameba 官方库发送字符串
                        testKeyboard.keySequence("Hello World !");
                        delay(50); // 给点时间发包
                    }

                    // 动效恢复：发送完 500ms 后恢复提示语
                    if (now - last_type_time > 500 && strcmp(status_msg, "Press OK to type") != 0) {
                        strcpy(status_msg, "Press OK to type");
                    }

                    // 绘制界面
                    canvas->clear();
                    drawHeader(canvas, "CONNECTED!");
                    canvas->drawText(10, 25, status_msg, 1);
                    canvas->drawText(10, 45, "[OK]   -> Type", 1);
                    canvas->drawText(10, 55, "[BACK] -> Exit", 1);
                    break;
            }

            canvas->display();
            delay(16);
        }

        // 退出 App，彻底关闭蓝牙广播
        BLE.configAdvert()->stopAdv();
        BLE.deinit();
    }
};

#endif