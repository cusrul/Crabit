#ifndef AMEBA_STORE_H
#define AMEBA_STORE_H

#include <stdint.h>
#include <queue.h>

class AmebaStore {
public:
    static float imu_ax, imu_ay, imu_az;
    static int opt_dx, opt_dy;
    static bool opt_lifted;
    static int battery_pct;
    static uint8_t raw_btn_mask;
    static int current_ui_event; // compatibility mirror; new code uses the queue
    static QueueHandle_t ui_event_queue;

    static void initRuntimeQueues() {
        if (!ui_event_queue) ui_event_queue = xQueueCreate(8, sizeof(int));
    }

    static void pushUiEvent(int event) {
        if (ui_event_queue) xQueueSend(ui_event_queue, &event, 0);
    }

    static int popUiEvent() {
        int event = 0;
        if (ui_event_queue && xQueueReceive(ui_event_queue, &event, 0) == pdTRUE) return event;
        return 0;
    }
};

#endif
