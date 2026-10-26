#ifndef APP_UART_H
#define APP_UART_H

#include "NanoUI/NanoUI.h"
#include "usart.h" 
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;

static char tx_str[32] = "Wait Send...";
static char rx_str[32] = "Wait RX...";
static char expected_rx[32] = "";
static char status_str[16] = "-";

static uint32_t ping_count = 0;
static uint32_t last_send_time = 0; 

class RenderUartDebug : public RenderObject {
public:
    void performLayout() override {
        size = {128, 50}; 
    }

    void paintWorker(Canvas* canvas, Point p) override {

        // 1. 鎵弿鎸夐敭
        uint8_t btn1 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5);
        uint8_t btn2 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6);
        uint8_t btn3 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7);
        
        if (btn1 == GPIO_PIN_RESET || btn2 == GPIO_PIN_RESET || btn3 == GPIO_PIN_RESET) {
            if (HAL_GetTick() - last_send_time > 500) { 
                last_send_time = HAL_GetTick();
                ping_count++;
                
                sprintf(tx_str, "PING %lu", ping_count); 
                sprintf(expected_rx, "ACK_PING %lu", ping_count);

                char msg[32];
                sprintf(msg, "PING %lu\n", ping_count);
                
                // ==========================================
                // 馃毃 鐙欏嚮妯″紡锛氭竻鐞嗗瀮鍦?-> 鍙戦€?-> 鍫垫鎺ユ敹锛?
                // ==========================================
                
                // 1. 寮鸿娓呴櫎涓插彛鐨勨€滄孩鍑?姝婚攣鈥濋敊璇爣蹇楋紝璁?HAL 搴撴弧琛€澶嶆椿
                __HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_OREF);
                uint8_t dump;
                while(HAL_UART_Receive(&huart1, &dump, 1, 0) == HAL_OK); // 娓呯┖娈嬩綑鍨冨溇

                // 2. 鍙戦€?PING
                HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), 100);

                // 3. 绔嬪埢姝荤瓑鎺ユ敹锛屾瘡涓瓧鑺傛渶澶氱瓑 20ms銆?
                char rx_buffer[64] = {0};
                int rx_ptr = 0;
                uint8_t temp_rx;
                
                // 鍙杩樿兘璇诲埌鏁版嵁锛屽氨涓€鐩磋锛岀洿鍒拌鍒版崲琛岀鎴栬秴鏃?
                while (HAL_UART_Receive(&huart1, &temp_rx, 1, 20) == HAL_OK) {
                    if (temp_rx == '\n') {
                        break; // 鏀跺埌鍥炶溅鎹㈣锛岃鏄?BW16 璇濊瀹屼簡锛?
                    }
                    if (temp_rx >= 0x20 && temp_rx <= 0x7E && rx_ptr < 30) {
                        rx_buffer[rx_ptr++] = temp_rx;
                    }
                }

                // 4. 鍒ゆ柇鎺ユ敹缁撴灉骞舵瘮瀵?
                if (rx_ptr > 0) {
                    rx_buffer[rx_ptr] = '\0'; 
                    strcpy(rx_str, rx_buffer);
                    
                    if (strstr(rx_buffer, expected_rx) != NULL) {
                        strcpy(status_str, "[ MATCH ]"); 
                    } else {
                        strcpy(status_str, "[ ERROR ]"); 
                    }
                } else {
                    // 濡傛灉绛変簡 20ms 鍟ヤ篃娌℃敹鍒?
                    strcpy(rx_str, "NO_RESPONSE");
                    strcpy(status_str, "[ TIMEOUT ]");
                }
            }
        }

        // ==========================================
        // 馃帹 缁樺埗 UI (杩欓儴鍒嗕繚鎸佷笉鍙?
        // ==========================================
        canvas->drawText(p.x, p.y, "--- UART VERIFY ---", 1);
        canvas->drawText(p.x, p.y + 12, "TX:", 1);
        canvas->drawText(p.x + 20, p.y + 12, tx_str, 1);
        canvas->drawText(p.x, p.y + 24, "RX:", 1);
        canvas->drawText(p.x + 20, p.y + 24, rx_str, 1);
        canvas->drawText(p.x, p.y + 36, "RES:", 1);
        
        if (strcmp(status_str, "[ MATCH ]") == 0) {
            canvas->fillRect(p.x + 24, p.y + 35, 60, 10, C_WHITE);
            canvas->drawText(p.x + 26, p.y + 36, status_str, 0); 
        } else {
            canvas->drawText(p.x + 24, p.y + 36, status_str, 1);
        }

        if(WindowManager::instance) WindowManager::instance->markDirty();
    }
};

class UartWidget : public Widget {
public:
    RenderObject* createRenderObject() override { return new RenderUartDebug(); }
};

class UartApp : public Application {
public:
    const char* getAppName() override { return "UART_DBG"; }
    Widget* buildUI() override {
        return new Container(
            new Column(NanoUI::EmbeddedVector<Widget*>{
                new Spacer(),
                new UartWidget(),
                new SizedBox(0, 4),
                new Row(NanoUI::EmbeddedVector<Widget*>{new Spacer(), new Text("Press ANY Key", 1), new Spacer()}),
                new Spacer()
            }),
            128, 64, 0, true, 2
        );
    }
};

#endif

