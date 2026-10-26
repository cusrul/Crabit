#pragma once

// Deliberately small host boundary for the actual STM32 SettingsApp and
// ConfigManager headers. No firmware HAL, RTOS, radio, or device is linked.
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef __MAIN_H
#define __MAIN_H
#endif

#ifndef FLASH_BASE
#define FLASH_BASE 0x08000000u
#endif
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 2048u
#endif

typedef struct {
    uint32_t TypeErase;
    uint32_t Banks;
    uint32_t Page;
    uint32_t NbPages;
} FLASH_EraseInitTypeDef;

#define FLASH_TYPEERASE_PAGES 1u
#define FLASH_BANK_1 1u
#define FLASH_TYPEPROGRAM_DOUBLEWORD 2u
#define HAL_OK 0
#define HAL_ERROR 1

extern unsigned settings_flat_flash_unlock_calls;
extern unsigned settings_flat_flash_erase_calls;
extern unsigned settings_flat_flash_program_calls;
extern unsigned settings_flat_program_fail_call;
extern unsigned settings_flat_flash_lock_calls;
extern uint8_t settings_flat_flash_image[56];
extern int settings_flat_unlock_status;
extern int settings_flat_lock_status;
extern int settings_flat_erase_status;
extern int settings_flat_program_status;

inline int HAL_FLASH_Unlock() { ++settings_flat_flash_unlock_calls; return settings_flat_unlock_status; }
inline int HAL_FLASH_Lock() { ++settings_flat_flash_lock_calls; return settings_flat_lock_status; }
inline int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef*, uint32_t* page_error) {
    ++settings_flat_flash_erase_calls;
    if (settings_flat_erase_status == HAL_OK && page_error) *page_error = 0xFFFFFFFFu;
    return settings_flat_erase_status;
}
inline int HAL_FLASH_Program(uint32_t, uint32_t address, uint64_t word) {
    ++settings_flat_flash_program_calls;
    if (settings_flat_program_status != HAL_OK) return settings_flat_program_status;
    if (settings_flat_program_fail_call == settings_flat_flash_program_calls) return HAL_ERROR;
    if (address >= 0x0803F800u &&
        address <= 0x0803F800u + sizeof(settings_flat_flash_image) - sizeof(word)) {
        std::memcpy(settings_flat_flash_image + address - 0x0803F800u, &word, sizeof(word));
    }
    return settings_flat_program_status;
}

extern unsigned settings_flat_oled_calls;
extern uint8_t settings_flat_oled_bytes[512];
extern uint8_t settings_flat_oled_commands[512];
