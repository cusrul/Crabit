#include "health.h"

#include <atomic>

#include "FreeRTOS.h"
#include "main.h"
#include "task.h"

namespace {
constexpr uint32_t BackupMagic = 0x43524200u;  // "CRB\0"
// BKP0R belongs to the RTC calendar initialization marker (rtc.c).
volatile uint32_t& faultBackupRegister() { return RTC->BKP1R; }
std::atomic<uint32_t> beats[static_cast<unsigned>(Health::Task::Count)];
std::atomic<bool> started{false};
std::atomic<uint32_t> started_at{0};
std::atomic<uint32_t> last_check{0};

void enableBackupAccess() {
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
}
}  // namespace

[[noreturn]] void Fault::reset(Reason reason) {
    __disable_irq();
    enableBackupAccess();
    faultBackupRegister() = BackupMagic | static_cast<uint32_t>(reason);
    NVIC_SystemReset();
    for (;;) {
    }
}

Fault::Reason Fault::lastBoot() {
    enableBackupAccess();
    const uint32_t saved = faultBackupRegister();
    if ((saved & 0xFFFFFF00u) != BackupMagic) {
        HAL_PWR_DisableBkUpAccess();
        return Reason::None;
    }
    faultBackupRegister() = 0;
    HAL_PWR_DisableBkUpAccess();
    const uint8_t code = static_cast<uint8_t>(saved & 0xFFu);
    return code <= static_cast<uint8_t>(Reason::HardFault) ? static_cast<Reason>(code)
                                                           : Reason::Boot;
}

extern "C" void Crabit_BootFault(void) {
    Fault::reset(Fault::Reason::Boot);
}

extern "C" void Crabit_HardFault(void) {
    Fault::reset(Fault::Reason::HardFault);
}

void Health::init() {
    const uint32_t now = HAL_GetTick();
    for (auto& beat : beats)
        beat.store(now, std::memory_order_relaxed);
    started_at.store(now, std::memory_order_relaxed);
    last_check.store(now, std::memory_order_relaxed);
    started.store(false, std::memory_order_relaxed);
}

void Health::start() {
    const uint32_t now = HAL_GetTick();
    for (auto& beat : beats)
        beat.store(now, std::memory_order_relaxed);
    started_at.store(now, std::memory_order_relaxed);
    last_check.store(now, std::memory_order_relaxed);
    started.store(true, std::memory_order_relaxed);
}

void Health::beat(Task task) {
    const unsigned index = static_cast<unsigned>(task);
    if (index < static_cast<unsigned>(Task::Count))
        beats[index].store(HAL_GetTick(), std::memory_order_relaxed);
}

void Health::resume() {
    const uint32_t now = HAL_GetTick();
    for (auto& beat : beats)
        beat.store(now, std::memory_order_relaxed);
    last_check.store(now, std::memory_order_relaxed);
}

void Health::check(uint32_t now) {
    if (!started.load(std::memory_order_relaxed))
        return;
    if (uint32_t(now - started_at.load(std::memory_order_relaxed)) < GraceMs)
        return;
    if (uint32_t(now - last_check.load(std::memory_order_relaxed)) < 1000)
        return;
    last_check.store(now, std::memory_order_relaxed);
    for (unsigned i = 0; i < static_cast<unsigned>(Task::Count); ++i) {
        // The TX worker blocks indefinitely when the queue is empty; a stale
        // beat there is normal. Send failures are visible through Link stats.
        if (i == static_cast<unsigned>(Task::Tx))
            continue;
        if (uint32_t(now - beats[i].load(std::memory_order_relaxed)) > TimeoutMs)
            Fault::reset(Fault::Reason::Task);
    }
}

uint32_t Health::lastBeat(Task task) {
    const unsigned index = static_cast<unsigned>(task);
    return index < static_cast<unsigned>(Task::Count)
               ? beats[index].load(std::memory_order_relaxed)
               : 0;
}
