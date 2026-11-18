#include "usart.h"
#include "usb.h"
#include "uart_owner.h"
#include "main.h"  // 提供 GPIOA 句柄与 HAL_GetTick
#include "axp2101.h"
#include "bw16_voltage_config.h"
#include <string.h>
#define SYS_USB_MODE 1
#ifndef SYS_USB_MODE
#define SYS_USB_MODE 0
#endif


extern "C" {
    volatile uint8_t bw16_flash_mode = 0;
    volatile uint8_t crabit_pat_hold = 0;
    


    // 流量统计
    volatile uint32_t uart_to_usb_count = 0;
    volatile uint32_t usb_to_uart_count = 0;

    #define RX_BUF_SIZE 2048
    uint8_t uart_rx_ring_buf[RX_BUF_SIZE];
    volatile uint16_t ring_last_head = 0; 
    extern DMA_HandleTypeDef hdma_usart1_rx;


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

    // ==========================================
    // 独占闸门：FLASH / BYPASS 都必须先取得 USART1 与 CDC 的唯一所有权。
    // ==========================================
    extern "C" bool BW16_Enter_Bypass() {
        if (!UartOwner_Claim(UART_OWNER_BYPASS)) return false;
        BW16_Release_Bus();
        if (huart1.gState != HAL_UART_STATE_RESET) {
            (void)UartOwner_Release(UART_OWNER_BYPASS);
            USB_LOG("[BW16] BYPASS release failed\r\n");
            return false;
        }
        return true;
    }

    extern "C" bool BW16_Exit_Bypass() {
        const bool ok = UartOwner_Release(UART_OWNER_BYPASS);
        if (!ok) USB_LOG("[BW16] BYPASS UART restore failed\r\n");
        return ok;
    }

    static bool BW16_Bridge_RxReady() {
        hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;
        if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK) return false;
        // MspInit/DeInit may re-enable the normal USART1 IRQ. The bridge polls
        // its own counter and error flags; no normal callback may run here.
        HAL_NVIC_DisableIRQ(USART1_IRQn);
        HAL_NVIC_DisableIRQ(DMA2_Channel7_IRQn);
        NVIC_ClearPendingIRQ(USART1_IRQn);
        NVIC_ClearPendingIRQ(DMA2_Channel7_IRQn);
        return HAL_UART_Receive_DMA(&huart1, uart_rx_ring_buf, RX_BUF_SIZE) == HAL_OK;
    }

    static bool BW16_Bridge_SetBaud(uint32_t baudrate) {
        if (baudrate == 0) return false;
        if (baudrate == huart1.Init.BaudRate) {
            UartOwner_SetLineCodingBaud(baudrate);
            return true;
        }
        // The owner task is the only context here; normal RX/TX IRQs are gated.
        if (HAL_UART_AbortReceive(&huart1) != HAL_OK &&
            huart1.RxState != HAL_UART_STATE_READY)
            return false;
        if (HAL_UART_DeInit(&huart1) != HAL_OK) return false;
        huart1.Init.BaudRate = baudrate;
        if (HAL_UART_Init(&huart1) != HAL_OK) return false;
        if (!BW16_Bridge_RxReady()) return false;
        ring_last_head = 0;
        UartOwner_SetLineCodingBaud(baudrate);
        return true;
    }

    static void BW16_Bridge_PowerDown() {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, GPIO_PIN_SET);
        AXP2101_SetDC3Enable(false);
    }



#if SYS_USB_MODE == 1
// ==========================================
    // 只有在 CDC 模式下，这些烧录代码才会被编译！
    // ==========================================

    // ==========================================
    // 模式 B：USB 与 BW16 UART 的专用桥接循环
    // ==========================================
    bool BW16_Dedicated_Bridge() {
        // 0. 先取得独占权：任何普通 RX/TX 都必须在改引脚/波特率前停下。
        if (!UartOwner_Claim(UART_OWNER_FLASH)) {
            if (Usb::rxReady()) Usb::consumeRx();
            USB_LOG("[BW16] bridge refused: UART owner busy\r\n");
            return false;
        }
        // 二进制烧录期间只允许桥本身写 CDC；日志与诊断必须保持静默。
        bw16_flash_mode = 1;
        // The configured CDC OUT endpoint is already armed. Once the owner
        // flips, its callback holds the first raw packet until we consume it.

        // 1. 预清理：让所有引脚进入高阻态，防止电量倒灌
        BW16_Release_Bus();
        if (huart1.gState != HAL_UART_STATE_RESET) {
            bw16_flash_mode = 0;
            BW16_Bridge_PowerDown();
            (void)UartOwner_Release(UART_OWNER_FLASH);
            if (Usb::rxReady()) Usb::consumeRx();
            USB_LOG("[BW16] bridge UART release failed\r\n");
            return false;
        }
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

        // 4. 打开 BW16 电源域：DCDC3 使用已保存档位。必须先供电，后接总线，避免未供电倒灌导致复位。
        AXP2101_SetDC3Voltage(Crabit::Bw16Voltage::targetMv());
        AXP2101_SetDC3Enable(true);
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

        bool bridge_ok = huart1.gState == HAL_UART_STATE_READY;
        if (bridge_ok) bridge_ok = BW16_Bridge_RxReady();

        // 清理状态变量
        uart_to_usb_count = 0;
        usb_to_uart_count = 0;
        ring_last_head = 0;

        // 主机可能在进入桥接前就设置过 line coding；在转发第一包前应用，
        // 保证 esptool 的数据与新波特率同步。
        uint32_t pending_baudrate = 0;
        if (bridge_ok && UartOwner_TakePendingLineCoding(&pending_baudrate))
            bridge_ok = BW16_Bridge_SetBaud(pending_baudrate);

        if (!bridge_ok) {
            bw16_flash_mode = 0;
            BW16_Bridge_PowerDown();
            (void)UartOwner_Release(UART_OWNER_FLASH);
            if (Usb::rxReady()) Usb::consumeRx();
            USB_LOG("[BW16] bridge UART setup failed\r\n");
            return false;
        }

        bool bridge_failed = false;
        bool requested_exit = false;

        // 6. 无限循环：在 USB 与 BW16 之间双向搬运数据
        while (1) {
            // line coding 由 USB 控制中断记录，只在本任务的安全点应用。
            uint32_t baudrate = 0;
            if (UartOwner_TakePendingLineCoding(&baudrate)) {
                if (!BW16_Bridge_SetBaud(baudrate)) {
                    bridge_failed = true;
                    break;
                }
            }

            // 清除 UART 硬件错误（溢出/帧错误）
            // 防止 1.5M 波特率下的偶发干扰导致 DMA 永久死机
            if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) ||
                __HAL_UART_GET_FLAG(&huart1, UART_FLAG_NE) ||
                __HAL_UART_GET_FLAG(&huart1, UART_FLAG_FE) ||
                __HAL_UART_GET_FLAG(&huart1, UART_FLAG_PE)) {
                
                __HAL_UART_CLEAR_FLAG(&huart1, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
                HAL_UART_AbortReceive(&huart1);
                ring_last_head = 0;
                if (HAL_UART_Receive_DMA(&huart1, uart_rx_ring_buf, RX_BUF_SIZE) != HAL_OK) {
                    bridge_failed = true;
                    break;
                }
            }

            // --- A. UART -> USB (接收芯片回传) ---
            uint16_t current_head = RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
            if (current_head >= RX_BUF_SIZE) current_head = 0;

            if (current_head != ring_last_head) {
                uint16_t len = (current_head > ring_last_head) ? (current_head - ring_last_head) : (RX_BUF_SIZE - ring_last_head);
                if (len > Usb::MaxTransfer) len = Usb::MaxTransfer;
                if (Usb::transmitBridge(&uart_rx_ring_buf[ring_last_head], len)) {
                    uart_to_usb_count += len; 
                    ring_last_head = (ring_last_head + len) % RX_BUF_SIZE;
                }
            }

            // --- B. USB -> UART (发送烧录数据) ---
            if (Usb::rxReady()) {
                // 阻塞发送，确保数据包完整抵达 BW16
                const uint32_t usb_len = Usb::rxLength();
                if (HAL_UART_Transmit(&huart1, const_cast<uint8_t*>(Usb::rxData()), usb_len,
                                      HAL_MAX_DELAY) != HAL_OK) {
                    // 不静默丢包：停止桥接并由上层可见地退出。
                    (void)HAL_UART_AbortTransmit(&huart1);
                    bridge_failed = true;
                    break;
                }
                usb_to_uart_count += usb_len;
                Usb::consumeRx(); // 处理完了，通知电脑发下一包
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
                    requested_exit = true;
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

        // 先关闭桥接/CDC，再断电，最后恢复普通 DMA RX；恢复成功后才重新
        // 打开普通流量。恢复失败保持 FAULT，绝不放行半初始化的 UART。
        bw16_flash_mode = 0;
        BW16_Bridge_PowerDown();
        const bool restored = UartOwner_Release(UART_OWNER_FLASH);
        // Discard a host packet that was captured for the raw bridge and
        // re-arm the endpoint only after the normal owner is back.
        if (Usb::rxReady()) Usb::consumeRx(); // 防锁死

        // 死等彻底松手，防止退出后误触上一级界面的按钮
        if (requested_exit) {
            while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET) {}
        }

        if (!restored) {
            USB_LOG("[BW16] FLASH UART restore failed\r\n");
            return false;
        }
        if (bridge_failed) {
            USB_LOG("[BW16] bridge stopped on UART failure\r\n");
            return false;
        }
        return true;
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
