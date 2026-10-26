#pragma once
#include <stdint.h>

namespace I2cRecovery {
enum class Result : uint32_t { None, Idle, Released, SclLow, SdaLow, ProbeFailed, InitFailed };

// Port owns the already-locked, disabled peripheral. GPIO writes must be open drain.
// No clock pulses for an initially low SCL or an already idle bus.
template <typename Port>
Result releaseBus(Port& port) {
    if (!port.scl())
        return Result::SclLow;
    if (port.sda())
        return Result::Idle;
    port.gpioOpenDrain();
    Result result = Result::SdaLow;
    for (unsigned pulse = 0; pulse < 9; ++pulse) {
        port.sclLow();
        port.pause();
        port.sclRelease();
        if (!port.waitScl()) {
            result = Result::SclLow;
            break;
        }
        port.pause();
        if (port.sda()) {
            // Generate STOP without a spurious START: SDA falls only with SCL low.
            port.sclLow();
            port.sdaLow();
            port.pause();
            port.sclRelease();
            if (!port.waitScl()) {
                result = Result::SclLow;
                break;
            }
            port.pause();
            port.sdaRelease();
            port.pause();
            result = port.scl() && port.sda() ? Result::Released : Result::SdaLow;
            break;
        }
    }
    port.releaseOutputs();
    return result;
}

// A failed episode gets at most three attempts and one GPIO-clear sequence.
// A successful probe alone does not reset the budget: require stable sampling.
class Budget {
    uint32_t delay_, deadline_ = 0, healthySince_ = 0, wake_ = 0;
    unsigned attempts_ = 0;
    bool pending_ = false, clearUsed_ = false, verifying_ = false;

   public:
    explicit Budget(uint32_t delay) : delay_(delay) {}
    bool syncWake(uint32_t wake) {
        if (wake_ == wake)
            return false;
        wake_ = wake;
        attempts_ = 0;
        pending_ = clearUsed_ = verifying_ = false;
        return true;
    }
    void fault(uint32_t now) {
        if (!pending_) {
            pending_ = true;
            deadline_ = now + delay_;
        }
        verifying_ = false;
    }
    bool due(uint32_t now) const {
        return pending_ && attempts_ < 3 && static_cast<int32_t>(now - deadline_) >= 0;
    }
    void beginAttempt(uint32_t now) {
        ++attempts_;
        deadline_ = now + delay_;
    }
    bool consumeClear() {
        if (clearUsed_)
            return false;
        clearUsed_ = true;
        return true;
    }
    void probePassed(uint32_t now) {
        pending_ = false;
        verifying_ = true;
        healthySince_ = now;
    }
    void healthy(uint32_t now) {
        if (verifying_ && uint32_t(now - healthySince_) >= delay_) {
            attempts_ = 0;
            clearUsed_ = verifying_ = false;
        }
    }
    unsigned attempts() const { return attempts_; }
    bool held() const { return pending_ && attempts_ >= 3; }
};
}  // namespace I2cRecovery
