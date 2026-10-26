#ifndef APP_CYBER_LINK_H
#define APP_CYBER_LINK_H

#include "../src/NanoUI.h"
#include "event_bus.h"
#include "app_coproc.h" 
#include "hw_input.h"
#include "hw_sensors.h"

class CyberLinkApp : public Application {
public:
    const char* getAppName() override { return "AMEBA_LINK"; }
    
    // 独占模式，接管一切！
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        int state = 0; 
        uint32_t state1_start_time = 0; 
        uint32_t anim_timer = 0;
        int dot_cnt = 0; 

        // 新增：组合键硬重启定时器
        uint32_t hard_reset_timer = 0; 

        // 数据累加器
        int16_t accum_dx = 0;
        int16_t accum_dy = 0;
        uint8_t accum_btn_mask = 0; 
        uint32_t last_sensor_sync = 0;

        while (running) {
            if (state == 0) {
                canvas->clear(); canvas->drawRoundRect(4, 16, 120, 32, 4, C_WHITE);
                canvas->drawText(10, 28, "1. WAKING AMEBA...", 1); canvas->display(); 
                BW16_NormalBoot(); 
                state = 1; state1_start_time = HAL_GetTick(); anim_timer = HAL_GetTick();
            }
            else if (state == 1) {
                if (HAL_GetTick() - anim_timer > 300) {
                    anim_timer = HAL_GetTick(); dot_cnt = (dot_cnt + 1) % 4;
                    canvas->clear(); canvas->drawRoundRect(4, 16, 120, 32, 4, C_WHITE);
                    if (dot_cnt == 0) canvas->drawText(10, 28, "2. AWAIT READY", 1);
                    if (dot_cnt == 1) canvas->drawText(10, 28, "2. AWAIT READY.", 1);
                    if (dot_cnt == 2) canvas->drawText(10, 28, "2. AWAIT READY..", 1);
                    if (dot_cnt == 3) canvas->drawText(10, 28, "2. AWAIT READY...", 1);
                    canvas->display(); 
                }
                if (HAL_GetTick() - state1_start_time > 1200) { state = 2; }
            }
            else if (state == 2) {
                canvas->clear(); canvas->drawRoundRect(4, 16, 120, 32, 4, C_WHITE);
                canvas->drawText(10, 28, "3. HANDING OVER!", 1); canvas->display(); 
                vTaskDelay(pdMS_TO_TICKS(150)); 
                canvas->clear(); canvas->display(); 
                
                Safe_SPI_Release(); 
                g_coproc_active = true; 
                EventBus::publish("sys/mode", "UI"); 
                
                vTaskDelay(pdMS_TO_TICKS(150)); 
                state = 3; 
            }
            else if (state == 3) {
                // ==========================================
                // --- 1. 离散事件累加 (找回 QMI 体感脉冲和光学滑动！) ---
                // ==========================================
                UIEvent e;
                while ((e = input->poll()).type != EVT_NONE) {
                    if (e.type == EVT_UP)    accum_btn_mask |= (1 << 0);
                    if (e.type == EVT_DOWN)  accum_btn_mask |= (1 << 1);
                    if (e.type == EVT_LEFT)  accum_btn_mask |= (1 << 2);
                    if (e.type == EVT_RIGHT) accum_btn_mask |= (1 << 3);
                    if (e.type == EVT_OK)    accum_btn_mask |= (1 << 4);
                    if (e.type == EVT_BACK)  accum_btn_mask |= (1 << 5);
                }

                // ==========================================
                // --- 1.5 终极安全门：物理硬核组合键夺权 ---
                // ==========================================
                uint32_t now = HAL_GetTick();
                // 只有当你同时死死按住 OK(PA5 低有效) 和 BACK(PH3 高有效) 时
                if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5) == GPIO_PIN_RESET && 
                    HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_SET) {
                    
                    if (hard_reset_timer == 0) {
                        hard_reset_timer = now; 
                    } else if (now - hard_reset_timer > 2000) { 
                        // 长按超过 2 秒，触发夺权！
                        state = 4; 
                        hard_reset_timer = 0;
                        // 播放一个高亢的“滴——”声，提示硬重启成功
                        SysAudio::play(4000, 200); 
                    }
                } else {
                    hard_reset_timer = 0; // 松开任何一个键就打断施法
                }

                // ==========================================
                // --- 2. 累加高频物理增量 (激光鼠标位移) ---
                // ==========================================
                NanoUI::Sensor* pat = NanoUI::SensorManager::getInstance().getSensor("PAT9125");
                int lift = 1;
                if (pat) { 
                    auto* pdata = static_cast<const NanoUI::SensorDataOptical*>(pat->getData()); 
                    accum_dx += pdata->dx; 
                    accum_dy += pdata->dy; 
                    lift = pdata->lifted ? 1 : 0; 
                }

                // ==========================================
                // --- 3. 严格 50Hz (20ms) 组装全量连续状态 JSON 发送 ---
                // ==========================================
                if (now - last_sensor_sync >= 20) { 
                    last_sensor_sync = now;

                    // 核心神操作：将“脉冲虚拟按键(QMI/PAT)”与“连续物理按键(GPIO)”完美叠加！
                    uint8_t tx_mask = accum_btn_mask;
                    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET) tx_mask |= (1 << 0); // UP   持续按压
                    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET) tx_mask |= (1 << 1); // DOWN 持续按压
                    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5) == GPIO_PIN_RESET) tx_mask |= (1 << 4); // OK    持续按压
                    if (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_SET)   tx_mask |= (1 << 5); // BACK  持续按压

                    // 取 IMU 数据
                    float ax = 0, ay = 0, az = 0; 
                    NanoUI::Sensor* imu = NanoUI::SensorManager::getInstance().getSensor("QMI8658");
                    if (imu) { 
                        auto* idata = static_cast<const NanoUI::SensorDataIMU*>(imu->getData()); 
                        ax = idata->ax; ay = idata->ay; az = idata->az; 
                    }
                    
                    // 取 电池 数据
                    int bat = 0;
                    NanoUI::Sensor* batt = NanoUI::SensorManager::getInstance().getSensor("BATTERY");
                    if (batt) { 
                        auto* bdata = static_cast<const NanoUI::SensorDataBattery*>(batt->getData()); 
                        bat = bdata->percent; 
                    }

                    // 纯净的字符串分隔符发数据 (发送的是融合后的 tx_mask!)
                    char val_buf[64]; 
                    snprintf(val_buf, sizeof(val_buf), "%d,%d,%d|%d,%d,%d|%d|%d", 
                        (int)(ax*100), (int)(ay*100), (int)(az*100), 
                        accum_dx, accum_dy, lift, 
                        bat, tx_mask); 
                    
                    EventBus::publish("sns/all", val_buf);

                    // 发送完毕，清空当前周期的离散累加器！(不用清空 tx_mask，那是局部的)
                    accum_dx = 0;
                    accum_dy = 0;
                    accum_btn_mask = 0;
                }
            }
            else if (state == 4) {
                // 强制重启 BW16 夺回 SPI 屏幕
                BW16_SetPower(false); 
                vTaskDelay(pdMS_TO_TICKS(50));  
                BW16_SetPower(true);            
                vTaskDelay(pdMS_TO_TICKS(250)); 
                state = 5;
            }
            else if (state == 5) {
                // 重新接管硬件
                Safe_SPI_Claim(); 
                HAL_SPI_DeInit(&hspi3); vTaskDelay(pdMS_TO_TICKS(5)); HAL_SPI_Init(&hspi3);
                extern void OLED_Init(void);
                extern void OLED_SetSleep(bool sleep);
                OLED_Init(); 
                OLED_SetSleep(false); 
                vTaskDelay(pdMS_TO_TICKS(50)); 
                g_coproc_active = false; 

                canvas->clear();
                canvas->drawRoundRect(4, 16, 120, 32, 4, C_WHITE);
                canvas->drawText(10, 28, "STM32 RECOVERED!", 1);
                canvas->display(); 
                
                vTaskDelay(pdMS_TO_TICKS(800)); 
                if (WindowManager::instance) WindowManager::instance->markDirty(); 
                running = false; 
            }
            vTaskDelay(pdMS_TO_TICKS(5)); 
        }
        AppManager::exitCurrentApp(); 
    }
};

#endif