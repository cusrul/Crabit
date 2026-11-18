#ifndef BADUSB_ARMORY_H
#define BADUSB_ARMORY_H

#include <stdint.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "flash_store.h"

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

public:
    // 供其他模块复用的 Flash Cache 刷新。
    static void flush_flash_cache() {
        FlashStore::flushCaches();
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
        FlashStore::Transaction tx;
        if (tx.entered()) {
            tx.erasePages(ARMORY_INDEX_PAGE, 1);
            tx.erasePages(ARMORY_PAYLOAD_PAGE, 6);
            tx.close();
        }
        next_index_addr = ARMORY_INDEX_ADDR;
        next_payload_addr = ARMORY_PAYLOAD_ADDR;
        flush_flash_cache();
    }

    static bool storePayload(uint32_t exec_time, const char* script, uint8_t type) {
        const uint16_t len = strlen(script);
        if (next_index_addr + sizeof(PayloadTask) >= ARMORY_INDEX_ADDR + 2048) return false;
        if (next_payload_addr + len + 8 >= ARMORY_INDEX_ADDR) return false;

        FlashStore::Transaction tx;
        if (!tx.entered()) return false;

        // 写入前清除残留的 Flash 错误标志，否则编程操作会被拒绝。
        FlashStore::clearErrors();

        const uint32_t current_payload_addr = next_payload_addr;
        for (uint16_t i = 0; i < len; i += 8) {
            const uint16_t chunk_size = (len - i > 8) ? 8 : (len - i);
            if (tx.program(next_payload_addr, script + i, chunk_size) != FlashStore::Result::Ok) {
                tx.close();
                flush_flash_cache();
                return false;
            }
            next_payload_addr += 8;
        }

        PayloadTask new_task{};
        new_task.magic = 0xDEADBEEF;
        new_task.execute_time = exec_time;
        new_task.payload_addr = current_payload_addr;
        new_task.length = len;
        new_task.type = type;
        new_task.status = 0xFF;

        if (tx.program(next_index_addr, &new_task, sizeof(new_task)) != FlashStore::Result::Ok) {
            tx.close();
            flush_flash_cache();
            return false;
        }
        next_index_addr += sizeof(PayloadTask);

        tx.close();
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
