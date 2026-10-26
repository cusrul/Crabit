#ifndef APP_BLE_MOUSE_H
#define APP_BLE_MOUSE_H

#include "../src/NanoUI.h"
#include "../AmebaStore.h"
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEHIDMouse.h>

// 实例化官方鼠标对象
BLEHIDMouse bleMouse;

class AppBleMouse : public Application {
private:
    enum AppState { 
        STATE_INIT,         // 初始化蓝牙 HID
        STATE_WAIT_CONN,    // 广播并等待主机连接
        STATE_SELECT_MODE,  // 已连接，让用户选择传感器
        STATE_RUN_OPTICAL,  // 光学鼠标模式 (PAT)
        STATE_RUN_GYRO      // 飞鼠模式 (QMI)
    };
    
    AppState currentState = STATE_INIT;
    uint32_t lastTick = 0;
    bool mouseActive = false; 

    void drawHeader(Canvas* canvas, const char* title) {
        canvas->fillRect(0, 0, 128, 12, C_WHITE);
        canvas->drawText(2, 2, title, 1, true);
    }

public:
    const char* getAppName() override { return "BLE_MOUSE"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        char buf[32];
        currentState = STATE_INIT;

        while(running && is_display_active) {
            UIEvent e = input->poll();
            
            // 按 BACK 键退出
            if (e.type == EVT_BACK ) {
                running = false;
                break;
            }

            switch(currentState) {
                
                case STATE_INIT: { 
                    canvas->clear();
                    drawHeader(canvas, "BLE HID INIT");
                    canvas->drawText(10, 30, "Configuring...");
                    canvas->display();

                    // 1. 打扫战场：停止之前的普通蓝牙服务
                    BLE.configAdvert()->stopAdv();
                    BLE.deinit(); 
                    delay(200);

                    // ==========================================
                    // 1:1 照搬官方例程初始化序列
                    // ==========================================
                    BLEAdvertData advdata;
                    advdata.addFlags(); 
                    advdata.addCompleteName("BW16_AirMouse"); 
                    advdata.addAppearance(GAP_GATT_APPEARANCE_HUMAN_INTERFACE_DEVICE); 
                    advdata.addCompleteServices(BLEUUID(HID_SERVICE_UUID));

                    // 必须先初始化 HID，再初始化 BLE
                    BLEHIDDev.init();
                    BLE.init();

                    // 配置广播和设备信息
                    BLE.configAdvert()->setAdvData(advdata);
                    BLE.setDeviceName("BW16_AirMouse");
                    BLE.setDeviceAppearance(GAP_GATT_APPEARANCE_HUMAN_INTERFACE_DEVICE);
                    
                    // 强制加密和绑定 (Windows 强制要求)
                    BLE.configSecurity()->setPairable(true);
                    BLE.configSecurity()->setAuthFlags(GAP_AUTHEN_BIT_BONDING_FLAG); 
                    
                    // 挂载 3 个必需的 HID 服务通道
                    BLE.configServer(3);
                    BLE.addService(BLEHIDDev.hidService());
                    BLE.addService(BLEHIDDev.battService());
                    BLE.addService(BLEHIDDev.devInfoService());

                    // 开始接客
                    BLE.beginPeripheral(); 
                    
                    currentState = STATE_WAIT_CONN;
                    break;
                }

                case STATE_WAIT_CONN:
                    if (BLE.connected()) {
                        currentState = STATE_SELECT_MODE;
                    }
                    
                    canvas->clear();
                    drawHeader(canvas, "PAIRING...");
                    canvas->drawText(5, 25, "Waiting for PC/Pad");
                    canvas->drawText(5, 45, "Name: BW16_AirMouse");
                    break;

                case STATE_SELECT_MODE:
                    if (!BLE.connected()) { currentState = STATE_WAIT_CONN; break; }

                    if (e.type == EVT_UP) currentState = STATE_RUN_OPTICAL;
                    if (e.type == EVT_DOWN) currentState = STATE_RUN_GYRO;

                    canvas->clear();
                    drawHeader(canvas, "CONNECTED!");
                    canvas->drawText(10, 20, "Select Sensor:");
                    canvas->drawText(15, 35, "[UP] Optical");
                    canvas->drawText(15, 50, "[DN] Gyro (IMU)");
                    
                    AmebaStore::opt_dx = 0; AmebaStore::opt_dy = 0;
                    mouseActive = false;
                    break;

                case STATE_RUN_OPTICAL:
                case STATE_RUN_GYRO:
                    if (!BLE.connected()) { currentState = STATE_WAIT_CONN; break; }

                    // OK 键用于“按下接管 / 松开暂停” 
                    if (e.type == EVT_OK ) {
                        mouseActive = !mouseActive; 
                    }
                    
                    int dx = 0, dy = 0;

                    // 鼠标移动发包逻辑 (限制发包频率防堵塞，约 50Hz)
                    if (mouseActive && (millis() - lastTick > 20)) {
                        if (currentState == STATE_RUN_OPTICAL) {
                            if (!AmebaStore::opt_lifted) {
                                dx = AmebaStore::opt_dx;
                                dy = AmebaStore::opt_dy;
                            }
                            AmebaStore::opt_dx = 0; AmebaStore::opt_dy = 0;
                        } 
                        else if (currentState == STATE_RUN_GYRO) {
                            // 陀螺仪映射，加死区防抖
                            dx = (int)(AmebaStore::imu_ax * -3.0f);
                            dy = (int)(AmebaStore::imu_ay * 3.0f);
                            if (abs(dx) < 2) dx = 0;
                            if (abs(dy) < 2) dy = 0;
                        }

                        // 调用 Ameba 官方库移动鼠标
                        if (dx != 0 || dy != 0) {
                            bleMouse.mouseMove(dx, dy);
                        }
                        lastTick = millis();
                    }

                    // UI 绘制
                    canvas->clear();
                    drawHeader(canvas, currentState == STATE_RUN_OPTICAL ? "OPTICAL MOUSE" : "GYRO MOUSE");
                    
                    snprintf(buf, sizeof(buf), "X:%-4d Y:%-4d", dx, dy);
                    canvas->drawText(15, 25, buf, 1);

                    if (mouseActive) {
                        canvas->fillRect(10, 41, 108, 11, C_WHITE);
                        canvas->drawText(15, 43, ">>> ENGAGED <<<", 1, true);
                    } else {
                        canvas->drawText(15, 43, "--- STANDBY ---", 1);
                    }
                    break;
            }

            canvas->display();
            delay(16); // 降低帧率，节约算力给蓝牙
        }

        // 退出 App 时关闭鼠标广播，清理战场
        BLE.configAdvert()->stopAdv();
        BLE.deinit();
    }
};

#endif