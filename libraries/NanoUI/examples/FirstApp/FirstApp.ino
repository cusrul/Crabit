#include "src/apps/hello_app.h"
#include "src/ports/serial_port.h"

namespace {
SerialCanvas canvas;
SerialInput input;
HelloApp app;
WindowManager* manager = nullptr;
uint32_t getTicks() { return millis(); }
void delayMs(uint32_t ms) { delay(ms); }
}

void setup() {
    Serial.begin(115200);
    HAL_Time::init(getTicks, delayMs);
    input.init();
    manager = new WindowManager(&canvas, &input);
    manager->push(app.buildUI(), &app, [](Application* owner) { return owner->buildUI(); });
    manager->drawFrame();
}

void loop() {
    manager->update();
    manager->drawFrame();
    delay(16);
}
