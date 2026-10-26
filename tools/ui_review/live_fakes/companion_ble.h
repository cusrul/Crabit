#pragma once
namespace Crabit {
namespace CompanionBle {
inline bool fakeActive = false;
inline const char* fakeStatus = "OFF";
inline bool active() {
    return fakeActive;
}
inline bool transitioning() {
    return false;
}
inline bool toggle() {
    return false;
}
inline const char* status() {
    return fakeStatus;
}
}  // namespace CompanionBle
}  // namespace Crabit
