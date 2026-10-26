#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <deque>

enum AppMode { APP_MODE_EXCLUSIVE, APP_MODE_FRAME };
static const int C_WHITE = 1;
inline int get_utf8_string_width(const char* value, int) {
    int width = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p)
        if ((*p & 0xC0) != 0x80) width += *p < 0x80 ? 4 : 8;
    return width;
}
class Canvas {
public:
    int displayCalls = 0;
    std::vector<std::string> texts;
    std::vector<std::string> writes;
    void clear() { writes.push_back("clear"); }
    void drawRoundRect(int, int, int, int, int, int) { writes.push_back("rect"); }
    void drawText(int, int, const char* text, int) { texts.push_back(text); writes.push_back(std::string("text:") + text); }
    void display() { ++displayCalls; writes.push_back("display"); }
    bool saw(const char* text) const { for (auto& t : texts) if (t == text) return true; return false; }
    bool lastWriteBefore(const char* value) const { return !writes.empty() && writes.back() == value; }
    bool sawWriteAfter(const char* value) const { for (auto& w : writes) if (w == value) return true; return false; }
};
enum UIEventType { EVT_NONE, EVT_UP, EVT_DOWN, EVT_LEFT, EVT_RIGHT, EVT_OK, EVT_BACK };
struct UIEvent { UIEventType type; int value; };
class InputDriver { public: virtual ~InputDriver() {} virtual UIEvent poll() = 0; };
class Application {
public:
    virtual ~Application() {}
    virtual const char* getAppName() = 0;
    virtual AppMode getMode() = 0;
    virtual void onMount(Canvas*, InputDriver*) {}
    virtual void onUpdate(uint32_t) {}
    virtual void onUnmount() {}
    void requestExit();
};
class WindowManager {
public:
    static WindowManager* instance;
    int dirties = 0;
    int frames = 0;
    void markDirty() { ++dirties; }
    void drawFrame() { ++frames; }
};
class Popup {
public:
    static std::vector<std::string>& messages() { static std::vector<std::string> items; return items; }
    static void showToast(const char* message) { messages().push_back(message); }
    static bool saw(const char* message) {
        for (const auto& item : messages()) if (item == message) return true;
        return false;
    }
};
namespace NanoUI {
struct SensorData { uint32_t timestamp=0; bool is_valid=false; virtual ~SensorData() {} };
struct SensorDataIMU : SensorData { float ax=0, ay=0, az=0; };
struct SensorDataOptical : SensorData { int dx=0, dy=0; bool lifted=true; };
struct SensorDataBattery : SensorData { int percent=0; };
class Sensor { public: virtual ~Sensor() {} virtual const SensorData* getData() const=0; };
extern Sensor* fake_pat_sensor;
class SensorManager {
public:
    static SensorManager& getInstance() { static SensorManager x; return x; }
    Sensor* getSensor(const char* name) {
        return name && strcmp(name, "PAT9125") == 0 ? fake_pat_sensor : nullptr;
    }
};
}
void SysAudio_play(int, int);
namespace SysAudio { inline void play(int hz, int ms) { SysAudio_play(hz, ms); } }
