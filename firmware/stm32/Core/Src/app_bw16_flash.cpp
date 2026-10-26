#include "usart.h"
#include "main.h"  // 提供 GPIOA 句柄与 HAL_GetTick
#include "axp2101.h"
#include <string.h>
#define SYS_USB_MODE 1
#ifndef SYS_USB_MODE
#define SYS_USB_MODE 0
#endif


extern "C" {
    volatile uint8_t bw16_flash_mode = 0;
    extern I2C_HandleTypeDef hi2c1;
    


    // 流量统计
    volatile uint32_t uart_to_usb_count = 0;
    volatile uint32_t usb_to_uart_count = 0;

    #define RX_BUF_SIZE 2048
    uint8_t uart_rx_ring_buf[RX_BUF_SIZE];
    volatile uint16_t ring_last_head = 0; 

    // 外部 USB 变量
    extern volatile uint8_t usb_rx_ready;
    extern volatile uint32_t usb_rx_len;
    extern uint8_t UserRxBufferFS[]; 
    void CDC_Resume_Receive(void); 

    // ==========================================
    // 模式 A：引脚彻底释放 (预防漏电流)
    // ==========================================
    void BW16_Release_Bus() {
        HAL_UART_DeInit(&huart1);
        GPIO_InitTypeDef GPIO_InitStruct{};
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG; // 设为模拟输入，彻底切断路径
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        
        GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7; // PB6 -> BW16 LOG_RX, PB7 -> BW16 LOG_TX/BOOT
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        
        GPIO_InitStruct.Pin = GPIO_PIN_2;              // PA2 (EN)
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }

    void BW16_Restore_Bus() {
        MX_USART1_UART_Init(); 
    }



#if SYS_USB_MODE == 1
// ==========================================
    // 只有在 CDC 模式下，这些烧录代码才会被编译！
    // ==========================================
    #include "usbd_cdc_if.h"



    // ==========================================
    // 模式 B：USB 与 BW16 UART 的专用桥接循环
    // ==========================================
    void BW16_Dedicated_Bridge() {
        // 1. 预清理：让所有引脚进入高阻态，防止电量倒灌
        BW16_Release_Bus();
        HAL_Delay(200); 

        GPIO_InitTypeDef GPIO_InitStruct{};

        // 2. 准备 BOOT：拉低 PB7 (BW16 LOG_TX/BOOT)，让 BW16 进入下载模式
        GPIO_InitStruct.Pin = GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET); 
        HAL_Delay(500);

        // 3. 断电复位：拉低 PA2 (EN)
        GPIO_InitStruct.Pin = GPIO_PIN_2;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
        HAL_Delay(500); // 确保 BW16 内部彻底放电

        // 4. 打开 BW16 电源域：DCDC3 = 3.0V。必须先供电，后接总线，避免未供电倒灌导致复位。
        AXP2101_SetDC3Voltage(&hi2c1, 3000);
        AXP2101_SetDC3Enable(&hi2c1, true);
        HAL_Delay(100);

        // 5. 保留老版本 PA13 低/高/低节奏，但放在 DCDC3 稳定之后。
        GPIO_InitStruct.Pin = GPIO_PIN_13;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, GPIO_PIN_RESET);
        HAL_Delay(100);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, GPIO_PIN_SET);
        HAL_Delay(100);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, GPIO_PIN_RESET);

        // 6. 上电启动
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET); 
        HAL_Delay(1000); // 稳稳等待 BW16 进入 Bootloader 模式

        // 7. 初始化串口硬件
        GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        
        MX_USART1_UART_Init();
        
        // 清理状态变量
        uart_to_usb_count = 0;
        usb_to_uart_count = 0;
        ring_last_head = 0;
        usb_rx_ready = 0;
        
        extern DMA_HandleTypeDef hdma_usart1_rx;
        hdma_usart1_rx.Init.Mode = DMA_CIRCULAR; 
        HAL_DMA_Init(&hdma_usart1_rx);
        HAL_UART_Receive_DMA(&huart1, uart_rx_ring_buf, RX_BUF_SIZE);

        bw16_flash_mode = 1;
        
        // 清除一次可能存在的残留标志位，拉开 USB 闸门
        CDC_Resume_Receive();

        // 6. 无限循环：在 USB 与 BW16 之间双向搬运数据
        while (1) {
            
            // 清除 UART 硬件错误（溢出/帧错误）
            // 防止 1.5M 波特率下的偶发干扰导致 DMA 永久死机
            if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) ||
                __HAL_UART_GET_FLAG(&huart1, UART_FLAG_NE) ||
                __HAL_UART_GET_FLAG(&huart1, UART_FLAG_FE) ||
                __HAL_UART_GET_FLAG(&huart1, UART_FLAG_PE)) {
                
                __HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
                HAL_UART_AbortReceive(&huart1);
                ring_last_head = 0;
                HAL_UART_Receive_DMA(&huart1, uart_rx_ring_buf, RX_BUF_SIZE);
            }

            // --- A. UART -> USB (接收芯片回传) ---
            uint16_t current_head = RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
            if (current_head >= RX_BUF_SIZE) current_head = 0;

            if (current_head != ring_last_head) {
                uint16_t len = (current_head > ring_last_head) ? (current_head - ring_last_head) : (RX_BUF_SIZE - ring_last_head);
                if (len > APP_TX_DATA_SIZE) len = APP_TX_DATA_SIZE;
                if (CDC_Transmit_FS(&uart_rx_ring_buf[ring_last_head], len) == USBD_OK) {
                    uart_to_usb_count += len; 
                    ring_last_head = (ring_last_head + len) % RX_BUF_SIZE;
                }
            }

            // --- B. USB -> UART (发送烧录数据) ---
            if (usb_rx_ready) {
                // 阻塞发送，确保数据包完整抵达 BW16
                if (HAL_UART_Transmit(&huart1, UserRxBufferFS, usb_rx_len, HAL_MAX_DELAY) == HAL_OK) {
                    usb_to_uart_count += usb_rx_len; 
                }
                
                usb_rx_ready = 0; 
                CDC_Resume_Receive(); // 处理完了，通知电脑发下一包
            }

// ==========================================
            // C. 按键消抖：轮询 GPIO，不阻塞搬运
            // ==========================================
            static uint32_t btn_press_start = 0;
            static uint32_t btn_release_start = 0;
            
            // 假设 PA7 是退出按键 (低电平触发)
            if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET) {
                btn_release_start = 0; // 只要读到低电平，立刻打断“松手”倒计时
                
                if (btn_press_start == 0) {
                    btn_press_start = HAL_GetTick(); // 记录初次接触的时间
                } 
                else if (HAL_GetTick() - btn_press_start > 50) {
                    // 连续按住超过 50ms 视为确认退出
                    bw16_flash_mode = 0; 
                    
                    if (usb_rx_ready) {
                        usb_rx_ready = 0;
                        CDC_Resume_Receive(); // 防锁死
                    }
                    
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, GPIO_PIN_SET);
                    AXP2101_SetDC3Enable(&hi2c1, false);
                    BW16_Restore_Bus(); 
                    
                    // 死等彻底松手，防止退出后误触上一级界面的按钮
                    while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET) {} 
                    break; 
                }
            } else {
                // 读到高电平后需持续 20ms 才认为真正松手，避免弹片颤振
                if (btn_press_start != 0) {
                    if (btn_release_start == 0) {
                        btn_release_start = HAL_GetTick(); // 开始考察是不是真松手
                    } 
                    else if (HAL_GetTick() - btn_release_start > 20) {
                        // 连续 20ms 高电平，重置按键计时
                        btn_press_start = 0; 
                    }
                }
            }
        }
    }

    #else
    // ==========================================
    // CDC 模式未启用时不提供桥接入口
    // ==========================================
    // void BW16_Dedicated_Bridge() {
    //     // 在键盘模式下，不可能通过 USB 传输固件数据
    //     // 直接返回，或者可以在这里弹出一个 OELD 提示框
    // }
#endif

}
