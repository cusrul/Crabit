#include "game_tone_service.h"

#include <atomic>

#include "FreeRTOS.h"
#include "task.h"

namespace CrabitTone {

namespace {

// The pair must be one snapshot: a consumer may not combine an old word with a
// newer timestamp (which would renew a stale cue) or a fresh word with an older
// timestamp. Both sides are ordinary tasks, so the existing platform critical
// section is shorter and simpler than a lock-free pair or a 64-bit atomic.
class CriticalSection {
public:
    CriticalSection() { taskENTER_CRITICAL(); }
    ~CriticalSection() { taskEXIT_CRITICAL(); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
};

// Single latest-value slot, always accessed as a coherent pair under
// CriticalSection. No queue, allocation or extra task is involved.
std::atomic<uint32_t> pending_word_{kEmptyWord};
std::atomic<uint32_t> pending_tick_{0};

static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "the handoff must stay lock-free");

} // namespace

bool handleFrame(const char* value, size_t length, uint16_t request_id,
                 bool session_active, uint32_t now_ms) {
    if (!session_active) {
        discardPending();
        return false;
    }
    if (request_id != 0) return false;

    Tone tone;
    const ParseResult parsed = parse(value, length, &tone);
    if (parsed == ParseResult::Invalid) return false;

    const uint32_t word = parsed == ParseResult::Stop ? kStopWord : encode(tone);
    {
        CriticalSection guard;
        // One protected publication: the timestamp belongs to this exact word.
        pending_tick_.store(now_ms, std::memory_order_relaxed);
        pending_word_.store(word, std::memory_order_relaxed);
    }
    return true;
}

uint32_t take(uint32_t now_ms) {
    uint32_t word;
    uint32_t posted;
    {
        CriticalSection guard;
        word = pending_word_.exchange(kEmptyWord, std::memory_order_relaxed);
        posted = pending_tick_.load(std::memory_order_relaxed);
    }
    if (word == kEmptyWord) return kEmptyWord;
    // A stop is a command, not an aged cue: an old stop must still take effect.
    if (word == kStopWord) return kStopWord;
    if (static_cast<uint32_t>(now_ms - posted) > kLeaseMs) return kEmptyWord;
    return word;
}

void discardPending() {
    CriticalSection guard;
    pending_word_.store(kEmptyWord, std::memory_order_relaxed);
}

void reset() {
    CriticalSection guard;
    pending_word_.store(kEmptyWord, std::memory_order_relaxed);
    pending_tick_.store(0, std::memory_order_relaxed);
}

} // namespace CrabitTone
