#pragma once
// Host stand-in for FreeRTOS task critical sections used by the game-tone
// handoff. It is a real mutual exclusion lock plus a deterministic pause hook,
// so a test can prove a consumer cannot observe an in-progress publication.

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace GameToneHostGate {

inline std::mutex lock_mutex;
inline std::mutex state_mutex;
inline std::condition_variable state_cv;
inline bool armed = false;
inline bool paused = false;
inline bool released = false;
inline int attempts = 0;

inline void enter() {
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        ++attempts;
    }
    state_cv.notify_all();
    lock_mutex.lock();

    bool pause = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (armed) {
            armed = false;
            paused = true;
            pause = true;
        }
    }
    if (!pause) return;

    state_cv.notify_all();
    std::unique_lock<std::mutex> lock(state_mutex);
    state_cv.wait(lock, [] { return released; });
}

inline void leave() {
    lock_mutex.unlock();
}

inline void arm() {
    std::lock_guard<std::mutex> lock(state_mutex);
    armed = true;
    paused = false;
    released = false;
    attempts = 0;
}

inline bool waitPaused() {
    std::unique_lock<std::mutex> lock(state_mutex);
    return state_cv.wait_for(lock, std::chrono::seconds(2), [] { return paused; });
}

inline bool waitAttempts(int count) {
    std::unique_lock<std::mutex> lock(state_mutex);
    return state_cv.wait_for(lock, std::chrono::seconds(2),
                             [count] { return attempts >= count; });
}

inline void release() {
    std::lock_guard<std::mutex> lock(state_mutex);
    released = true;
    state_cv.notify_all();
}

} // namespace GameToneHostGate

// The production source includes these macros from the real FreeRTOS headers.
#define taskENTER_CRITICAL() GameToneHostGate::enter()
#define taskEXIT_CRITICAL() GameToneHostGate::leave()
