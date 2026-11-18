#include "flash_store.h"

#include <string.h>

#include "FreeRTOS.h"
#include "main.h"
#include "semphr.h"

void* FlashStore::mutex_ = nullptr;

bool FlashStore::init() {
    if (mutex_ == nullptr)
        mutex_ = xSemaphoreCreateMutex();
    return mutex_ != nullptr;
}

FlashStore::Transaction::Transaction(uint32_t timeout_ms) {
    if (FlashStore::mutex_ == nullptr)
        return;
    if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(FlashStore::mutex_),
                       pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return;
    entered_ = true;
    if (HAL_FLASH_Unlock() != HAL_OK)
        result_ = Result::Unlock;
}

FlashStore::Transaction::~Transaction() {
    close();
}

FlashStore::Result FlashStore::Transaction::erasePages(uint32_t first_page, uint32_t count) {
    if (!entered_ || result_ != Result::Ok)
        return result_;
    FLASH_EraseInitTypeDef erase{};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = first_page;
    erase.NbPages = count;
    uint32_t page_error = 0xFFFFFFFFu;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK || page_error != 0xFFFFFFFFu)
        result_ = Result::Erase;
    return result_;
}

FlashStore::Result FlashStore::Transaction::program(uint32_t address, const void* data,
                                                    size_t length) {
    if (!entered_ || result_ != Result::Ok)
        return result_;
    if ((address & 7u) != 0 || (data == nullptr && length != 0)) {
        result_ = Result::Program;
        return result_;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t offset = 0; offset < length; offset += 8) {
        uint64_t word = 0xFFFFFFFFFFFFFFFFull;
        const size_t chunk = (length - offset < 8) ? length - offset : 8;
        memcpy(&word, bytes + offset, chunk);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + offset, word) != HAL_OK) {
            result_ = Result::Program;
            break;
        }
    }
    return result_;
}

FlashStore::Result FlashStore::Transaction::close() {
    if (closed_)
        return result_;
    closed_ = true;
    if (entered_) {
        if (HAL_FLASH_Lock() != HAL_OK && result_ == Result::Ok)
            result_ = Result::Lock;
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(FlashStore::mutex_));
    } else if (result_ == Result::Ok) {
        result_ = Result::Busy;
    }
    return result_;
}

FlashStore::Result FlashStore::read(uint32_t address, void* data, size_t length) {
    if (data == nullptr && length != 0)
        return Result::Program;
    memcpy(data, reinterpret_cast<const void*>(address), length);
    return Result::Ok;
}

void FlashStore::clearErrors() {
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
}

void FlashStore::flushCaches() {
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_RESET();
    __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
}
