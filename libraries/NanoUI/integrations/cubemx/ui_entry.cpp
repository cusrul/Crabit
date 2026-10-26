#include "ui_entry.h"
#include "board_port.h"
#include "hello_app.h"

namespace {
HelloApp app;
WindowManager* manager = nullptr;
}
void nanoui_init() {
    if (manager) return;
    HAL_Time::init(nanoui_ticks, nanoui_delay);
    nanoui_input().init();
    manager = new WindowManager(&nanoui_canvas(), &nanoui_input());
    manager->push(app.buildUI(), &app, [](Application* owner) { return owner->buildUI(); });
}
void nanoui_tick() {
    if (!manager) return;
    manager->update();
    manager->drawFrame();
}
