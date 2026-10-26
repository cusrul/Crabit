#ifndef BADUSB_ARMORY_H
#define BADUSB_ARMORY_H

#include <stdint.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"

// Flash 物理地址映射
#define ARMORY_INDEX_PAGE   62
#define ARMORY_INDEX_ADDR   0x0801F000  // 任务索引区 (2KB)
#define ARMORY_PAYLOAD_PAGE 56
#define ARMORY_PAYLOAD_ADDR 0x0801C000  // 脚本数据区 (12KB)

struct __attribute__((aligned(8))) PayloadTask {
    uint32_t magic;         // 0xDEADBEEF
    uint32_t execute_time;  
    uint32_t payload_addr;  
    uint16_t length;        
    uint8_t  type;          
    uint8_t  status;        
};

class BadUsbArmory {
private:
    static inline uint32_t next_index_addr = ARMORY_INDEX_ADDR;
    static inline uint32_t next_payload_addr = ARMORY_PAYLOAD_ADDR;

    static void Flash_Write_8Bytes(uint32_t addr, uint64_t data) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, data);
    }

public:
    // 供其他模块复用的 Flash Cache 刷新。
    static void flush_flash_cache() {
        __HAL_FLASH_DATA_CACHE_DISABLE();
        __HAL_FLASH_DATA_CACHE_RESET();
        __HAL_FLASH_DATA_CACHE_ENABLE();
        __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
        __HAL_FLASH_INSTRUCTION_CACHE_RESET();
        __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
    }

    static void init() {
        next_index_addr = ARMORY_INDEX_ADDR;
        next_payload_addr = ARMORY_PAYLOAD_ADDR;

        while (next_index_addr < (ARMORY_INDEX_ADDR + 2048)) {
            PayloadTask* task = (PayloadTask*)next_index_addr;
            if (task->magic == 0xFFFFFFFF) break; 
            
            if (task->magic == 0xDEADBEEF) {
                uint32_t payload_end = task->payload_addr + task->length;
                next_payload_addr = (payload_end + 7) & ~7; 
            }
            next_index_addr += sizeof(PayloadTask);
        }
    }

    static void clearArmory() {
        HAL_FLASH_Unlock();
        FLASH_EraseInitTypeDef erase;
        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.Banks = FLASH_BANK_1;
        uint32_t err = 0;
        
        erase.Page = ARMORY_INDEX_PAGE; erase.NbPages = 1; HAL_FLASHEx_Erase(&erase, &err);
        erase.Page = ARMORY_PAYLOAD_PAGE; erase.NbPages = 6; HAL_FLASHEx_Erase(&erase, &err);
        
        HAL_FLASH_Lock();
        
        next_index_addr = ARMORY_INDEX_ADDR;
        next_payload_addr = ARMORY_PAYLOAD_ADDR;
        flush_flash_cache();
    }

    static bool storePayload(uint32_t exec_time, const char* script, uint8_t type) {
        uint16_t len = strlen(script);
        if (next_index_addr + sizeof(PayloadTask) >= ARMORY_INDEX_ADDR + 2048) return false;
        if (next_payload_addr + len + 8 >= ARMORY_INDEX_ADDR) return false;

        HAL_FLASH_Unlock();
        
        // 写入前清除残留的 Flash 错误标志，否则编程操作会被拒绝。
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

        uint32_t current_payload_addr = next_payload_addr;
        uint8_t buffer[8];
        
        for (uint16_t i = 0; i < len; i += 8) {
            memset(buffer, 0xFF, 8); 
            uint16_t chunk_size = (len - i > 8) ? 8 : (len - i);
            memcpy(buffer, script + i, chunk_size);
            Flash_Write_8Bytes(next_payload_addr, *(uint64_t*)buffer);
            next_payload_addr += 8;
        }

        PayloadTask new_task;
        new_task.magic = 0xDEADBEEF;
        new_task.execute_time = exec_time;
        new_task.payload_addr = current_payload_addr;
        new_task.length = len;
        new_task.type = type;
        new_task.status = 0xFF; 

        uint64_t* task_dwords = (uint64_t*)&new_task;
        Flash_Write_8Bytes(next_index_addr, task_dwords[0]);
        Flash_Write_8Bytes(next_index_addr + 8, task_dwords[1]);
        
        next_index_addr += sizeof(PayloadTask);

        HAL_FLASH_Lock();
        flush_flash_cache();
        return true;
    }


    // 检查还有未执行的定时任务。
    static bool hasPendingTasks() {
        uint32_t check_addr = ARMORY_INDEX_ADDR;
        while (check_addr < (ARMORY_INDEX_ADDR + 2048)) {
            volatile PayloadTask* task = (volatile PayloadTask*)check_addr;
            if (task->magic == 0xFFFFFFFF) break; // 空白区表示后续无任务
            if (task->magic == 0xDEADBEEF) return true;
            check_addr += sizeof(PayloadTask);
        }
        return false; // 未发现带有效任务标记的记录。
    }
    
};

#endif
