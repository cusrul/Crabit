#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

#include <stdint.h>
#include <string.h>
#include "main.h" // 引入 HAL 库用于操作 Flash
#include "../../../shared/settings_contract.h"
#include "../../../shared/watch_face.h"
#include "../../../shared/product_preferences.h"
#include <stddef.h>
#include <math.h>

#define SYS_CONFIG_MAGIC 0xDEADBEE4u

// =========================================================
// 配置结构体：8 字节对齐，满足 L4 Flash 双字写入要求。
// =========================================================
struct __attribute__((aligned(8))) SystemConfig {
    // 标记位：用于判断 Flash 中是否已有有效配置。
    uint32_t magic_word = SYS_CONFIG_MAGIC; 

    // 1. 息屏与休眠时间 (毫秒)
    uint32_t screen_off_ms = 3000; 
    uint32_t kill_app_ms   = 5000; 
    uint32_t deep_sleep_ms = 10000; 
    
    // 2. 唤醒逻辑配置
    bool  enable_lift_wake = true; 
    bool  enable_tap_wake  = true; 
    
    // 3. 抬腕姿态 3D 阈值
    float wake_x_max = 0.3f;  
    float wake_y_min = -1.0f;  
    float wake_y_max = 1.0f;  
    float wake_z_min = 0.3f;  

    // 4. 屏幕配置
    float brightness = 0.5f;   
    bool  invert_disp = false; 

    // 5. 蓝牙连接配置
    bool enable_ble = false; 
    // Reuse the six existing tail-padding bytes. Older fields/Flash format stay put.
    uint8_t watch_face = 0;
    uint8_t watch_face_tag[5] = {0x57, 0x46, 0x01, 0xA5, 0x5A};
};

extern SystemConfig sys_cfg;

// =========================================================
// Flash 配置存储。
// =========================================================
// 256KB Flash: last 2KB page, page 127.
#define CONFIG_FLASH_ADDR 0x0803F800u
#define CONFIG_FLASH_PAGE ((CONFIG_FLASH_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE)

class ConfigManager {
public:
    enum class SaveResult { Saved, UnlockFailed, EraseFailed, ProgramFailed, LockFailed };

    static void normalizeWatchFace() {
        const uint8_t tag[5] = {0x57, 0x46, 0x01, 0xA5, 0x5A};
        if (memcmp(sys_cfg.watch_face_tag, tag, sizeof(tag)) != 0)
            sys_cfg.watch_face = 0;  // Legacy padding is not a saved preference.
        sys_cfg.watch_face = Crabit::validWatchFace(sys_cfg.watch_face);
        memcpy(sys_cfg.watch_face_tag, tag, sizeof(tag));
    }

    static const char* saveError(SaveResult result) {
        switch (result) {
        case SaveResult::Saved: return "none";
        case SaveResult::UnlockFailed: return "unlock";
        case SaveResult::EraseFailed: return "erase";
        case SaveResult::ProgramFailed: return "program";
        case SaveResult::LockFailed: return "lock";
        }
        return "unknown";
    }
    static void load() {
        SystemConfig* flash_cfg = (SystemConfig*)CONFIG_FLASH_ADDR;
        
        // 检查魔数：如果 Flash 里确实存过我们的配置，就拷贝到 RAM
        if (flash_cfg->magic_word == SYS_CONFIG_MAGIC) {
            memcpy(&sys_cfg, flash_cfg, sizeof(SystemConfig));
            normalizeWatchFace();
        } else {
            // 如果是新片子或者没存过，sys_cfg 保持默认值不变，不用管它
        }
        CrabitPreferences::Record trailer{};
        memcpy(&trailer, reinterpret_cast<const uint8_t*>(flash_cfg) + sizeof(SystemConfig), sizeof(trailer));
        // A missing/corrupt legacy base cannot make an orphan trailer trusted.
        CrabitPreferences::load(flash_cfg->magic_word == SYS_CONFIG_MAGIC ? trailer : CrabitPreferences::Record{});
    }

    static SaveResult save() {
        // Preserve the existing bytes/address/page/format. A failed erase or
        // partial program cannot be rolled back: RAM remains applied, but the
        // caller must NOT claim persistence. Always attempt to re-lock Flash.
        if (HAL_FLASH_Unlock() != HAL_OK) {
            HAL_FLASH_Lock();
            return SaveResult::UnlockFailed;
        }

        // 1. 擦除整页 (2KB)
        FLASH_EraseInitTypeDef erase{};
        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.Banks = FLASH_BANK_1;
        erase.Page = CONFIG_FLASH_PAGE;
        erase.NbPages = 1;
        
        uint32_t PageError = 0xFFFFFFFFu;
        if (HAL_FLASHEx_Erase(&erase, &PageError) != HAL_OK || PageError != 0xFFFFFFFFu) {
            HAL_FLASH_Lock();
            return SaveResult::EraseFailed;
        }

        // 2. 将 RAM 里的配置按双字 (64-bit) 写入 Flash
        size_t write_cnt = (sizeof(SystemConfig) + 7) / 8; // 向上取整计算写入次数
        
        for (size_t i = 0; i < write_cnt; i++) {
            uint64_t word;
            memcpy(&word, reinterpret_cast<const uint8_t*>(&sys_cfg) + i * 8, 8);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                                  CONFIG_FLASH_ADDR + (i * 8), word) != HAL_OK) {
                HAL_FLASH_Lock();
                return SaveResult::ProgramFailed;
            }
        }

        const auto preferences = CrabitPreferences::sealed();
        uint64_t trailer_word = 0;
        memcpy(&trailer_word, &preferences, sizeof(trailer_word));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                CONFIG_FLASH_ADDR + sizeof(SystemConfig), trailer_word) != HAL_OK) {
            HAL_FLASH_Lock();
            return SaveResult::ProgramFailed;
        }

        return HAL_FLASH_Lock() == HAL_OK ? SaveResult::Saved : SaveResult::LockFailed;
    }

    // Called only by the UI owner's EventBus drain. Never replace sys_cfg with
    // a client's stale snapshot; patch just the explicitly selected fields.
    static SaveResult apply(const CrabitSettings::Patch& patch) {
        if (patch.fields & CrabitSettings::Brightness) sys_cfg.brightness = patch.brightness / 1000.0f;
        if (patch.fields & CrabitSettings::Invert) sys_cfg.invert_disp = patch.invert != 0;
        if (patch.fields & CrabitSettings::ScreenOff) sys_cfg.screen_off_ms = patch.screen_off_ms;
        if (patch.fields & CrabitSettings::DeepSleep) sys_cfg.deep_sleep_ms = patch.deep_sleep_ms;
        return save();
    }

    static bool settingsResponse(char* value, size_t size, const char* result, const char* error,
                                 const char* display = "unchanged") {
        const float b = sys_cfg.brightness;
        const int brightness = isfinite(b) && b >= 0 && b <= 1 ? int(b * 1000 + 0.5f) : -1;
        return CrabitSettings::response(value, size, result, error, display, brightness,
                                       sys_cfg.invert_disp, sys_cfg.screen_off_ms, sys_cfg.deep_sleep_ms);
    }
};

static_assert(sizeof(SystemConfig) == 48, "Keep existing config Flash format");
static_assert(sizeof(SystemConfig) + sizeof(CrabitPreferences::Record) <= FLASH_PAGE_SIZE,
              "Preferences must stay inside the existing config page");
static_assert(offsetof(SystemConfig, brightness) == 36, "Keep old display settings offsets");
static_assert(offsetof(SystemConfig, enable_ble) == 41, "Keep old BLE settings offset");
static_assert(offsetof(SystemConfig, watch_face) == 42, "Use only old tail padding");

#endif
