// #ifndef BADUSB_EXECUTOR_H
// #define BADUSB_EXECUTOR_H

// #include "badusb_armory.h"
// #include "rtc.h"
// #include "FreeRTOS.h"
// #include "task.h"
// #include <string.h>
// #include <stdlib.h>
// #include <stdio.h>

// #include "usbd_customhid.h"
// extern USBD_HandleTypeDef hUsbDeviceFS;

// struct KeyboardReport {
//     uint8_t modifier;
//     uint8_t reserved;
//     uint8_t keys[6];
// };

// // 核心改进 1：阻塞式安全发送（解决按键粘连）
// static void Safe_HID_Send(struct KeyboardReport* report) {
//     // 如果 USB 忙，就死等，直到发出去为止。防止“松开按键”的数据包丢失
//     uint32_t timeout = 100; // 100ms 超时保护
//     while (USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, (uint8_t*)report, 8) == USBD_BUSY && timeout--) {
//         vTaskDelay(1); 
//     }
// }

// // 核心改进 2：简单的 ASCII 转 USB Keycode 映射函数
// static uint8_t ascii_to_hid(char c) {
//     if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
//     if (c >= 'A' && c <= 'Z') return 0x04 + (c - 'A'); // 注意：大写需要配合 Modifier Shift
//     if (c >= '1' && c <= '9') return 0x1E + (c - '1');
//     if (c == '0') return 0x27;
//     if (c == ' ') return 0x2C; // 重点：空格是 0x2C
//     if (c == '.') return 0x37;
//     if (c == '\n') return 0x28; // 回车
//     return 0;
// }

// static void HID_Keyboard_Send_Safe(uint8_t modifier, uint8_t key) {
//     KeyboardReport report = {modifier, 0, {key, 0, 0, 0, 0, 0}};
//     Safe_HID_Send(&report);
// }

// static void HID_Keyboard_ReleaseAll_Safe() {
//     KeyboardReport report = {0, 0, {0, 0, 0, 0, 0, 0}};
//     Safe_HID_Send(&report);
// }

// class BadUsbExecutor {
// private:
//     static inline bool executed_flags[128] = {false}; 

//     static void execute_line(char* line) {
//         if (strlen(line) < 2) return;
//         USB_LOG("  > Executing: %s\r\n", line);

//         if (strncmp(line, "DELAY ", 6) == 0) {
//             vTaskDelay(pdMS_TO_TICKS(atoi(line + 6)));
//         }
//         else if (strncmp(line, "STRING ", 7) == 0) {
//             char* str = line + 7;
//             while (*str) {
//                 uint8_t keycode = ascii_to_hid(*str);
//                 uint8_t modifier = (*str >= 'A' && *str <= 'Z') ? 0x02 : 0x00; // 简单大写处理

//                 if (keycode != 0) {
//                     HID_Keyboard_Send_Safe(modifier, keycode);
// vTaskDelay(pdMS_TO_TICKS(10)); // 必须有：模拟物理按键按下的时间
//                     HID_Keyboard_ReleaseAll_Safe();
// vTaskDelay(pdMS_TO_TICKS(10)); // 必须有：模拟物理按键松开的时间
//                 }
//                 str++;
//             }
//         }
//         else if (strncmp(line, "GUI ", 4) == 0) {
//             uint8_t keycode = ascii_to_hid(line[4]);
//             HID_Keyboard_Send_Safe(0x08, keycode); // 0x08 是 Win 键
//             vTaskDelay(pdMS_TO_TICKS(20));
//             HID_Keyboard_ReleaseAll_Safe();
//         }
//         else if (strcmp(line, "ENTER") == 0) {
//             HID_Keyboard_Send_Safe(0x00, 0x28);
//             vTaskDelay(pdMS_TO_TICKS(20));
//             HID_Keyboard_ReleaseAll_Safe();
//         }
        
// // 战术双保险：每行指令执行完，强制再次全释放，防止粘连
//         HID_Keyboard_ReleaseAll_Safe();
//     }

//     static void parse_and_execute(const char* script, uint16_t len) {
// USB_LOG(" [DETONATOR] Payload Triggered!\r\n");
//         char line_buf[128];
//         int line_pos = 0;

//         for (uint16_t i = 0; i <= len; i++) {
//             char c = (i < len) ? script[i] : '\n';
//             if (c == '\r') continue; 
//             if (c == '\n' || i == len) {
//                 line_buf[line_pos] = '\0';
//                 if (line_pos > 0) execute_line(line_buf); 
//                 line_pos = 0;
//             } else {
//                 if (line_pos < 127) line_buf[line_pos++] = c;
//             }
//         }
// // 终极保险：脚本全部跑完，强制释放所有按键
//         HID_Keyboard_ReleaseAll_Safe();
// USB_LOG(" [DETONATOR] Complete!\r\n");
//     }

// public:
//     static void Task_Detonator(void* pvParams) {
//         (void)pvParams;
//         while (1) {
//             RTC_TimeTypeDef sTime = {0};
//             RTC_DateTypeDef sDate = {0};
//             uint32_t current_hhmm = 0;

//             if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK) {
//                 HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN); 
//                 current_hhmm = sTime.Hours * 100 + sTime.Minutes;
//             }

//             BadUsbArmory::flush_flash_cache();
//             uint32_t check_addr = ARMORY_INDEX_ADDR; 
//             int task_idx = 0;

//             while (check_addr < (ARMORY_INDEX_ADDR + 2048) && task_idx < 128) {
//                 PayloadTask* task = (PayloadTask*)check_addr;
//                 if (task->magic == 0xFFFFFFFF) break; 
//                 if (task->magic == 0xDEADBEEF && !executed_flags[task_idx]) {
//                     if (task->execute_time == 0 || task->execute_time == current_hhmm) {
//                         parse_and_execute((const char*)task->payload_addr, task->length);
//                         executed_flags[task_idx] = true; 
//                     }
//                 }
//                 check_addr += sizeof(PayloadTask);
//                 task_idx++;
//             }
//             vTaskDelay(pdMS_TO_TICKS(1000));
//         }
//     }
// };

// #endif

















// #ifndef BADUSB_EXECUTOR_H
// #define BADUSB_EXECUTOR_H

// #include "badusb_armory.h"
// #include "rtc.h"
// #include "FreeRTOS.h"
// #include "task.h"
// #include <string.h>
// #include <stdlib.h>
// #include <stdio.h>

// extern "C" void USB_LOG(const char* format, ...);

// // 因为切回了 CDC 模式，所以我们把真键盘驱动屏蔽，换回串口打印演示
// inline void HID_Keyboard_Send(uint8_t modifier, uint8_t key) {
//     USB_LOG("[HID SIMULATION] Send -> Mod: %02X, Key: %02X\r\n", modifier, key);
// }

// inline void HID_Keyboard_ReleaseAll() {
//     USB_LOG("[HID SIMULATION] Release All\r\n");
// }

// class BadUsbExecutor {
// private:
//     static inline bool executed_flags[128] = {false}; 

//     static void execute_line(char* line) {
//         if (strlen(line) < 2) return;
//         USB_LOG("  > Executing: %s\r\n", line);

//         if (strncmp(line, "DELAY ", 6) == 0) {
//             int ms = atoi(line + 6);
//             vTaskDelay(pdMS_TO_TICKS(ms));
//         }
//         else if (strncmp(line, "STRING ", 7) == 0) {
//             char* str = line + 7;
//             while (*str) {
//                 HID_Keyboard_Send(0x00, *str); 
//                 HID_Keyboard_ReleaseAll();
//                 str++;
//                 vTaskDelay(pdMS_TO_TICKS(20)); 
//             }
//         }
//         else if (strncmp(line, "GUI ", 4) == 0) {
//             HID_Keyboard_Send(0x08, line[4]); 
//             HID_Keyboard_ReleaseAll();
//         }
//         else if (strcmp(line, "ENTER") == 0) {
//             HID_Keyboard_Send(0x00, 0x28); 
//             HID_Keyboard_ReleaseAll();
//         }
//     }

//     static void parse_and_execute(const char* script, uint16_t len) {
// USB_LOG(" [DETONATOR] Payload Triggered! (%d bytes)\r\n", len);

//         char line_buf[128];
//         int line_pos = 0;

//         for (uint16_t i = 0; i <= len; i++) {
//             char c = (i < len) ? script[i] : '\n';
//             if (c == '\r') continue; 
            
//             if (c == '\n' || i == len) {
//                 line_buf[line_pos] = '\0';
//                 if (line_pos > 0) execute_line(line_buf); 
//                 line_pos = 0;
//             } else {
//                 if (line_pos < 127) line_buf[line_pos++] = c;
//             }
//         }
// USB_LOG(" [DETONATOR] Execution Complete!\r\n");
//     }

// public:
//     static void Task_Detonator(void* pvParams) {
//         (void)pvParams;
// USB_LOG(" [SYS] BadUSB Detonator Task Started.\r\n");

//         while (1) {
//             RTC_TimeTypeDef sTime = {0};
//             RTC_DateTypeDef sDate = {0};
//             uint32_t current_hhmm = 0;

//             if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK) {
//                 HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN); 
//                 current_hhmm = sTime.Hours * 100 + sTime.Minutes;
//             }

//             BadUsbArmory::flush_flash_cache();

//             uint32_t check_addr = ARMORY_INDEX_ADDR; 
//             int task_idx = 0;

//             while (check_addr < (ARMORY_INDEX_ADDR + 2048) && task_idx < 128) {
//                 PayloadTask* task = (PayloadTask*)check_addr;

//                 if (task->magic == 0xFFFFFFFF) break; 

//                 if (task->magic == 0xDEADBEEF && !executed_flags[task_idx]) {
//                     if (task->execute_time == 0 || task->execute_time == current_hhmm) {
                        
//                         const char* script_ptr = (const char*)task->payload_addr;
//                         parse_and_execute(script_ptr, task->length);
//                         executed_flags[task_idx] = true; 
//                     }
//                 }
//                 check_addr += sizeof(PayloadTask);
//                 task_idx++;
//             }

//             vTaskDelay(pdMS_TO_TICKS(1000));
//         }
//     }
// };

// #endif




















#ifndef BADUSB_EXECUTOR_H
#define BADUSB_EXECUTOR_H

#include "badusb_armory.h"
#include "rtc.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern "C" void USB_LOG(const char* format, ...);

// 确保能读到全局模式宏
#ifndef SYS_USB_MODE
#define SYS_USB_MODE 0 // 默认设定为键盘刺客模式
#endif

#if SYS_USB_MODE == 1
// ====================================================================
// CDC 模式 (烧录模式)：没有物理键盘驱动，只能通过串口打印模拟击键
// ====================================================================
static void HID_Keyboard_Send_Safe(uint8_t modifier, uint8_t key) {
    USB_LOG("[HID SIMULATION] Send -> Mod: %02X, Key: %02X\r\n", modifier, key);
}

static void HID_Keyboard_ReleaseAll_Safe() {
    USB_LOG("[HID SIMULATION] Release All\r\n");
}

#else
// ====================================================================
// HID 模式 (刺客模式)：真实的物理键盘攻击
// ====================================================================
#include "usbd_customhid.h"
extern USBD_HandleTypeDef hUsbDeviceFS;

struct KeyboardReport {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keys[6];
};

// 阻塞式安全发送（解决按键粘连）
static void Safe_HID_Send(struct KeyboardReport* report) {
    uint32_t timeout = 100; // 100ms 超时保护
    while (USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, (uint8_t*)report, 8) == USBD_BUSY && timeout--) {
        vTaskDelay(1); 
    }
}

static void HID_Keyboard_Send_Safe(uint8_t modifier, uint8_t key) {
    KeyboardReport report = {modifier, 0, {key, 0, 0, 0, 0, 0}};
    Safe_HID_Send(&report);
}

static void HID_Keyboard_ReleaseAll_Safe() {
    KeyboardReport report = {0, 0, {0, 0, 0, 0, 0, 0}};
    Safe_HID_Send(&report);
}
#endif


// ====================================================================
// 共享核心逻辑：无论哪种模式，解析和定时的逻辑是一样的
// ====================================================================

// 简单的 ASCII 转 USB Keycode 映射函数
static uint8_t ascii_to_hid(char c) {
    if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
    if (c >= 'A' && c <= 'Z') return 0x04 + (c - 'A'); // 注意：大写需要配合 Modifier Shift
    if (c >= '1' && c <= '9') return 0x1E + (c - '1');
    if (c == '0') return 0x27;
    if (c == ' ') return 0x2C; // 重点：空格是 0x2C
    if (c == '.') return 0x37;
    if (c == '\n') return 0x28; // 回车
    return 0;
}

class BadUsbExecutor {
private:
    static inline bool executed_flags[128] = {false}; 

    static void execute_line(char* line) {
        if (strlen(line) < 2) return;
        USB_LOG("  > Executing: %s\r\n", line);

        if (strncmp(line, "DELAY ", 6) == 0) {
            vTaskDelay(pdMS_TO_TICKS(atoi(line + 6)));
        }
        else if (strncmp(line, "STRING ", 7) == 0) {
            char* str = line + 7;
            while (*str) {
                uint8_t keycode = ascii_to_hid(*str);
                uint8_t modifier = (*str >= 'A' && *str <= 'Z') ? 0x02 : 0x00; // 简单大写处理

                if (keycode != 0) {
                    HID_Keyboard_Send_Safe(modifier, keycode);
                    vTaskDelay(pdMS_TO_TICKS(10)); // 必须有：模拟物理按键按下的时间
                    HID_Keyboard_ReleaseAll_Safe();
                    vTaskDelay(pdMS_TO_TICKS(10)); // 必须有：模拟物理按键松开的时间
                }
                str++;
            }
        }
        else if (strncmp(line, "GUI ", 4) == 0) {
            uint8_t keycode = ascii_to_hid(line[4]);
            HID_Keyboard_Send_Safe(0x08, keycode); // 0x08 是 Win 键
            vTaskDelay(pdMS_TO_TICKS(20));
            HID_Keyboard_ReleaseAll_Safe();
        }
        else if (strcmp(line, "ENTER") == 0) {
            HID_Keyboard_Send_Safe(0x00, 0x28);
            vTaskDelay(pdMS_TO_TICKS(20));
            HID_Keyboard_ReleaseAll_Safe();
        }
        
        // 战术双保险：每行指令执行完，强制再次全释放，防止粘连
        HID_Keyboard_ReleaseAll_Safe();
    }

    static void parse_and_execute(const char* script, uint16_t len) {
        USB_LOG("🔥 [DETONATOR] Payload Triggered!\r\n");
        char line_buf[128];
        int line_pos = 0;

        for (uint16_t i = 0; i <= len; i++) {
            char c = (i < len) ? script[i] : '\n';
            if (c == '\r') continue; 
            if (c == '\n' || i == len) {
                line_buf[line_pos] = '\0';
                if (line_pos > 0) execute_line(line_buf); 
                line_pos = 0;
            } else {
                if (line_pos < 127) line_buf[line_pos++] = c;
            }
        }
        // 终极保险：脚本全部跑完，强制释放所有按键
        HID_Keyboard_ReleaseAll_Safe();
        USB_LOG("✅ [DETONATOR] Complete!\r\n");
    }

public:
    static void Task_Detonator(void* pvParams) {
        (void)pvParams;
        while (1) {
            RTC_TimeTypeDef sTime = {0};
            RTC_DateTypeDef sDate = {0};
            uint32_t current_hhmm = 0;

            if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK) {
                HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN); 
                current_hhmm = sTime.Hours * 100 + sTime.Minutes;
            }

            BadUsbArmory::flush_flash_cache();
            uint32_t check_addr = ARMORY_INDEX_ADDR; 
            int task_idx = 0;

            while (check_addr < (ARMORY_INDEX_ADDR + 2048) && task_idx < 128) {
                PayloadTask* task = (PayloadTask*)check_addr;
                if (task->magic == 0xFFFFFFFF) break; 
                if (task->magic == 0xDEADBEEF && !executed_flags[task_idx]) {
                    if (task->execute_time == 0 || task->execute_time == current_hhmm) {
                        parse_and_execute((const char*)task->payload_addr, task->length);
                        executed_flags[task_idx] = true; 
                    }
                }
                check_addr += sizeof(PayloadTask);
                task_idx++;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
};

#endif