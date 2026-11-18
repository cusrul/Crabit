#pragma once

// Product assembly: services, sensors, display, UART and background tasks.
// NanoUI_MainLoop only runs the UI loop after Board has wired everything.
class Board {
public:
    static void init();
    static void startTasks();
};
