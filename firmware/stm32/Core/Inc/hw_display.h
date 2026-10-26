#ifndef HW_DISPLAY_H
#define HW_DISPLAY_H

#include "NanoUI/NanoUI.h"
#include "spi.h" 
#include <string.h>

extern "C" void OLED_WR_Byte(uint8_t dat, uint8_t cmd);

// DC 引脚 PA15，与 freertos.c 中的 OLED 写入一致。
#define OLED_DC_SET_DATA() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET)

class STM32Canvas : public Canvas {
private:
    uint8_t buffer[1024];

public:
    void clear() override { memset(buffer, 0, 1024); }

    // 按颜色常量置位、清零或翻转对应像素位。
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        
        int index = x + (y / 8) * 128;
        uint8_t bit_mask = (1 << (y % 8));

        if (color == C_WHITE) {
            buffer[index] |= bit_mask;        // 置 1
        } else if (color == C_BLACK) {
            buffer[index] &= ~bit_mask;       // 清 0
        } else if (color == C_INVERT) {
            buffer[index] ^= bit_mask;        // 翻转
        }
    }

    void display() override {
        for (int i = 0; i < 8; i++) {
            OLED_WR_Byte(0xB0 + i, 0); 
            OLED_WR_Byte(0x00, 0);     
            OLED_WR_Byte(0x10, 0);     
            
            OLED_DC_SET_DATA(); 
            
            // 此时 DC 已拉高，DMA 搬运会把 buffer 内容当做 Data 刷进屏幕
            if (HAL_SPI_Transmit_DMA(&hspi3, &buffer[i * 128], 128) != HAL_OK) {
                HAL_SPI_Abort(&hspi3);
                return;
            }
            
            // 等待本次 Page 传输完成
            uint32_t transfer_start = HAL_GetTick();
            while (HAL_SPI_GetState(&hspi3) != HAL_SPI_STATE_READY) {
                if (HAL_GetTick() - transfer_start >= 100) {
                    HAL_SPI_Abort(&hspi3);
                    return;
                }
            }
        }
    }

    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
};

#endif
