#ifndef APP_BLE_PPT_H
#define APP_BLE_PPT_H

#include "../src/NanoUI.h"
#include "../AmebaStore.h"
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEHIDKeyboard.h>

// 实例化官方蓝牙键盘对象
BLEHIDKeyboard bleKeyboard;

class AppBlePPT : public Application {
private:
    enum AppState { STATE_INIT, STATE_WAIT_CONN, STATE_RUN };
    AppState currentState = STATE_INIT;
    
    uint32_t last_wave_time = 0;
    
    // 挥手调校参数
    const float WAVE_THRESH = 1.2f; 
    const uint32_t WAVE_COOLDOWN = 600; 

    void drawHeader(Canvas* canvas, const char* title) {
        canvas->fillRect(0, 0, 128, 12, C_WHITE);
        canvas->drawText(2, 2, title, 1, true);
    }

public:
    const char* getAppName() override { return "BLE_PPT"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        char status_msg[32] = "STANDBY...";
        currentState = STATE_INIT;

        while(running && is_display_active) {
            UIEvent e = input->poll();
            
            // 按 BACK 退出并切断蓝牙
            if (e.type == EVT_BACK ) {
                running = false;
                break;
            }

            uint32_t now = millis();

            switch(currentState) {
                
                case STATE_INIT: { 
                    canvas->clear();
                    drawHeader(canvas, "BLE PPT INIT");
                    canvas->display();

                    // 1. 清理蓝牙环境
                    BLE.configAdvert()->stopAdv();
                    BLE.deinit(); 
                    delay(200);

                    // 2. 1:1 照搬你发的官方键盘例程
                    BLEAdvertData advdata;
                    advdata.addFlags(); 
                    advdata.addCompleteName("BW16_PPT_Remote"); // 电脑上显示的名字
                    advdata.addAppearance(GAP_GATT_APPEARANCE_HUMAN_INTERFACE_DEVICE); 
                    advdata.addCompleteServices(BLEUUID(HID_SERVICE_UUID));

                    BLEHIDDev.init();
                    BLE.init();

                    BLE.configAdvert()->setAdvData(advdata);
                    BLE.setDeviceName("BW16_PPT_Remote");
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
                    canvas->drawText(5, 40, "BW16_PPT_Remote");
                    break;

                case STATE_RUN:
                    if (!BLE.connected()) { currentState = STATE_WAIT_CONN; break; }

                    // 使用 STM32 通过 UART 更新的加速度数据。
                    float ay = AmebaStore::imu_ay;
                    float az = AmebaStore::imu_az;

                    // 核心算法：看表姿势 (az>0.6) + Y轴猛挥 (abs(ay)>1.2)
                    if (az > 0.6f && (now - last_wave_time > WAVE_COOLDOWN)) {
                        if (abs(ay) > WAVE_THRESH) {
                            last_wave_time = now;
                            
                            // 触发：发送键盘【右方向键】
                            // 0x4F 是 USB HID 协议里标准 Right Arrow 的十六进制码
                            // 也可以用 HID_KEY_RIGHT_ARROW (如果库里有宏定义)
                            bleKeyboard.keyPress(0x4F); 
                            delay(10); // 极短延时模拟手指按下
                            bleKeyboard.keyReleaseAll(); // 立即松开
                            
                            strcpy(status_msg, ">>> NEXT PAGE >>>");
                        }
                    }

                    // 动效恢复
                    if (now - last_wave_time > 400 && strcmp(status_msg, "STANDBY...") != 0) {
                        strcpy(status_msg, "STANDBY...");
                    }

                    canvas->clear();
                    drawHeader(canvas, "CONNECTED!");
                    canvas->drawText(10, 25, status_msg, 1);
                    
                    char debug_buf[16];
                    snprintf(debug_buf, sizeof(debug_buf), "Y Force: %.2f", ay);
                    canvas->drawText(10, 42, debug_buf, 1);
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
