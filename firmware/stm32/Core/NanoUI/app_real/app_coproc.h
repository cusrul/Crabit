#ifndef APP_COPROC_H
#define APP_COPROC_H

#include "NanoUI/NanoUI.h"
#include "usart.h" 
#include "spi.h"   
#include <string.h>
#include <stdio.h> 
#include "FreeRTOS.h"
#include "task.h"
#include "global_store.h" 
#include "event_bus.h" 
#include "axp2101.h"
#include "i2c.h"

extern "C" void OLED_Init(void);
extern void OLED_SetSleep(bool sleep); 
extern "C" void OLED_WR_Byte(uint8_t dat, uint8_t cmd);
extern volatile bool g_coproc_active; 
extern SPI_HandleTypeDef hspi3; 

extern volatile uint32_t dbg_rx_evts;
extern volatile uint32_t dbg_rx_errs;

inline void BW16_SetPower(bool enable) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

inline void BW16_SetBusConnect(bool connect) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, connect ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

inline void BW16_EnablePowerDomain() {
    AXP2101_SetDC3Voltage(&hi2c1, 3100);
    AXP2101_SetDC3Enable(&hi2c1, true);
}

inline void BW16_DisablePowerDomain() {
    AXP2101_SetDC3Enable(&hi2c1, false);
}

inline void BW16_PrimeBootState() {
    // 开机先把 BW16 相关状态归位，避免总线或 PMU 沿用上次的残留状态。
    BW16_SetBusConnect(false);
    BW16_SetPower(false);
    BW16_DisablePowerDomain();
    vTaskDelay(pdMS_TO_TICKS(20));
}

inline void BW16_ForceOff() {
    BW16_SetBusConnect(false);
    BW16_SetPower(false);
    vTaskDelay(pdMS_TO_TICKS(50));
    BW16_DisablePowerDomain();
}

inline void BW16_NormalBoot() {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    BW16_PrimeBootState();
    BW16_SetBusConnect(true);
    BW16_SetPower(false);  
    BW16_EnablePowerDomain();
    vTaskDelay(pdMS_TO_TICKS(150));        
    MX_USART1_UART_Init(); 
    BW16_SetPower(true);   
}

inline void Safe_SPI_Release() {
    HAL_SPI_DeInit(&hspi3);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG; 
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

inline void Safe_SPI_Claim() {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
}

class CoprocTerminalApp : public Application {
private:
    const char* mode_command; 
    bool is_ui_mode; 

    const char* bleStatusLabel() const {
        if (strcmp(mode_command, "BLE") != 0) return "";
        if (strncmp(GlobalStore.ble_status, "PAIR_CONFIRM", 12) == 0) return "PAIR WAIT: PRESS OK";
        if (strcmp(GlobalStore.ble_status, "PAIR_CONFIRMED") == 0) return "PAIR CONFIRMED";
        if (strcmp(GlobalStore.ble_status, "PAIR_TIMEOUT") == 0) return "PAIR TIMEOUT";
        if (strcmp(GlobalStore.ble_status, "DISCONNECTED") == 0) return "BLE DISCONNECTED";
        return "BLE READY";
    }

public:
    CoprocTerminalApp(const char* cmd) : mode_command(cmd) {
        is_ui_mode = (strcmp(mode_command, "UI") == 0);
    }
    
    const char* getAppName() override { return "BW16_DEBUG"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        int state = 0; 
        uint32_t state1_start_time = 0; 
        uint32_t anim_timer = 0;
        int dot_cnt = 0; 
        int force_quit_cnt = 0;
        uint32_t last_back_press = 0;

        while (running) {
            if (state == 0) {
                canvas->clear(); canvas->drawRoundRect(4, 16, 120, 32, 4, C_WHITE);
                canvas->drawText(10, 28, "1. PWR ON BW16...", 1); canvas->display(); 
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
                
                if (is_ui_mode) {
                    canvas->drawText(10, 28, "3. HANDING OVER!", 1); canvas->display(); 
                    vTaskDelay(pdMS_TO_TICKS(150)); 
                    canvas->clear(); canvas->display(); 
                    Safe_SPI_Release(); 
                    g_coproc_active = true; 
                } else {
                    canvas->drawText(10, 28, "3. MODE ACTIVE!", 1); canvas->display(); 
                }

                EventBus::publish("sys/mode", mode_command); 
                vTaskDelay(pdMS_TO_TICKS(150)); 
                state = 3; 
            }
            else if (state == 3) {
                UIEvent e = input->poll();
                if (e.type != EVT_NONE) {
                    if (is_ui_mode) { 
                        if (e.type == EVT_UP) EventBus::publish("sys/btn", "UP");
                        else if (e.type == EVT_DOWN) EventBus::publish("sys/btn", "DOWN");
                        else if (e.type == EVT_OK) EventBus::publish("sys/btn", "OK");
                    }
                    
                    if (e.type == EVT_BACK) {
                        if (is_ui_mode) EventBus::publish("sys/btn", "BACK");
                        
                        uint32_t now = HAL_GetTick();
                        if (now - last_back_press < 600) { 
                            force_quit_cnt++;
                            if (force_quit_cnt >= 3) { state = 4; } 
                        } else force_quit_cnt = 1;
                        last_back_press = now;
                    }
                }

                static uint32_t last_sensor_sync = 0;
                uint32_t now = HAL_GetTick();
                if (now - last_sensor_sync > 33) { 
                    last_sensor_sync = now;
                    float ax = 0, ay = 0, az = 0; int dx = 0, dy = 0, lift = 1, bat = 0;
                    NanoUI::Sensor* imu = NanoUI::SensorManager::getInstance().getSensor("QMI8658");
                    if (imu) { auto* idata = static_cast<const NanoUI::SensorDataIMU*>(imu->getData()); ax = idata->ax; ay = idata->ay; az = idata->az; }
                    NanoUI::Sensor* pat = NanoUI::SensorManager::getInstance().getSensor("PAT9125");
                    if (pat) { auto* pdata = static_cast<const NanoUI::SensorDataOptical*>(pat->getData()); dx = pdata->dx; dy = pdata->dy; lift = pdata->lifted ? 1 : 0; }
                    NanoUI::Sensor* batt = NanoUI::SensorManager::getInstance().getSensor("BATTERY");
                    if (batt) { auto* bdata = static_cast<const NanoUI::SensorDataBattery*>(batt->getData()); bat = bdata->percent; }
                    
                    char val_buf[64]; 
                    snprintf(val_buf, sizeof(val_buf), "%d,%d,%d|%d,%d,%d|%d", (int)(ax*100), (int)(ay*100), (int)(az*100), dx, dy, lift, bat);
                    EventBus::publish("sys/sns", val_buf);
                }

                if (!is_ui_mode) {
                    canvas->clear();
                    canvas->drawRoundRect(2, 2, 124, 60, 4, C_WHITE);
                    
                    char title_buf[32];
                    snprintf(title_buf, sizeof(title_buf), "%s ACTIVE & RUNNING", mode_command);
                    canvas->drawText(8, 10, title_buf, 1);
                    
                    char stat_buf[32];
                    snprintf(stat_buf, sizeof(stat_buf), "R:%lu E:%lu", dbg_rx_evts, dbg_rx_errs);
                    canvas->drawText(10, 26, stat_buf, 1);

                    canvas->drawText(10, 36, bleStatusLabel(), 1);
                    
                    canvas->drawText(10, 50, "Press BACKx3 to Exit", 1);
                    canvas->display();
                }
            }
            else if (state == 4) {
                BW16_ForceOff();
                vTaskDelay(pdMS_TO_TICKS(250)); 
                state = 5;
            }
            else if (state == 5) {
                if (is_ui_mode) {
                    Safe_SPI_Claim(); 
                    HAL_SPI_DeInit(&hspi3); vTaskDelay(pdMS_TO_TICKS(5)); HAL_SPI_Init(&hspi3);
                    OLED_Init(); 
                    OLED_SetSleep(false); 
                    vTaskDelay(pdMS_TO_TICKS(50)); 
                    g_coproc_active = false; 
                }

                canvas->clear();
                canvas->drawRoundRect(4, 16, 120, 32, 4, C_WHITE);
                canvas->drawText(10, 28, "STM32 RECOVERED!", 1);
                canvas->display(); 
                
                vTaskDelay(pdMS_TO_TICKS(800)); 
                if (WindowManager::instance) WindowManager::instance->markDirty(); 
                running = false;
            }
            vTaskDelay(pdMS_TO_TICKS(16)); 
        }
        AppManager::exitCurrentApp(); 
    }
};

static const uint8_t icon_wifi[] = { 0x00,0x00,0x07,0xe0,0x1f,0xf8,0x38,0x1c,0x70,0x0e,0xe3,0xc7,0xc7,0xe3,0x0f,0xf0,0x1c,0x38,0x18,0x18,0x03,0xc0,0x07,0xe0,0x00,0x00,0x01,0x80,0x01,0x80,0x00,0x00 };
static const uint8_t icon_ble[]  = { 0x01,0x80,0x01,0xc0,0x01,0x60,0x01,0x30,0x11,0x18,0x09,0x8c,0x04,0xc6,0x02,0x60,0x02,0x60,0x04,0xc6,0x09,0x8c,0x11,0x18,0x01,0x30,0x01,0x60,0x01,0xc0,0x01,0x80 };
static const uint8_t icon_ui[]   = { 0x3f,0xfc,0x20,0x04,0x20,0x04,0x27,0xe4,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x27,0xe4,0x20,0x04,0x20,0x04,0x3f,0xfc,0x00,0x00 };
static const uint8_t icon_slp[]  = { 0x00,0x00,0x00,0x00,0x1f,0xf8,0x00,0x38,0x00,0x70,0x00,0xe0,0x01,0xc0,0x03,0x80,0x07,0x00,0x0e,0x00,0x1c,0x00,0x38,0x00,0x1f,0xf8,0x00,0x00,0x00,0x00,0x00,0x00 };

class CoprocLinkApp : public Application {
private:
    Widget* buildCoprocItem(const uint8_t* icon, const char* title, VoidCallback onClick) {
        NanoUI::EmbeddedVector<Widget*> col_items;
        col_items.push_back(new Spacer());
        col_items.push_back(new Image(icon, 16, 16));
        col_items.push_back(new SizedBox(0, 4));
        col_items.push_back(new Text(title, 1));
        col_items.push_back(new Spacer());
        
        NanoUI::EmbeddedVector<Widget*> row_items;
        row_items.push_back(new Spacer());
        row_items.push_back(new Column(col_items));
        row_items.push_back(new Spacer());

        return new Button(new Container(new Row(row_items), 46, 40, 0, false, 4), onClick);
    }
public:
    const char* getAppName() override { return "COPROC"; }
    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> carousel_items;
        carousel_items.push_back(buildCoprocItem(icon_slp, "SLEEP", []() { BW16_ForceOff(); Popup::showToast("BW16 OFF"); }));
        carousel_items.push_back(buildCoprocItem(icon_ble, "BLE_TEST", []() { AppManager::launch(new CoprocTerminalApp("BLE")); }));
        carousel_items.push_back(buildCoprocItem(icon_wifi, "WIFI_TEST", []() { AppManager::launch(new CoprocTerminalApp("WIFI")); }));
        carousel_items.push_back(buildCoprocItem(icon_ui, "UI_TEST", []() { AppManager::launch(new CoprocTerminalApp("UI")); }));

        NanoUI::EmbeddedVector<Widget*> root_col;
        root_col.push_back(new SizedBox(0, 4));
        
        NanoUI::EmbeddedVector<Widget*> title_row;
        title_row.push_back(new Spacer());
        title_row.push_back(new Text("[ BW16 DEBUG ]", 1));
        title_row.push_back(new Spacer());
        root_col.push_back(new Row(title_row));
        
        root_col.push_back(new SizedBox(0, 6));
        
        NanoUITitleList empty_titles;
        root_col.push_back(new Carousel(carousel_items, empty_titles, 10));
        root_col.push_back(new Spacer());

        return new Container(new Column(root_col), 128, 64, 0, false, 0);
    }
};

#endif
