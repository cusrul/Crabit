#ifndef APP_BLE_SYNC_H
#define APP_BLE_SYNC_H

#include "../src/NanoUI.h"
#include "../AuditRelay.h"

extern volatile bool is_display_active;

// The legacy menu class name is retained; this App never initializes BLE.
class AppBleSync : public Application {
   public:
    const char* getAppName() override { return "记录中转"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        AuditRelay::begin();
        while (is_display_active) {
            const UIEvent event = input->poll();
            if (event.type == EVT_BACK)
                return;
            if (event.type == EVT_OK)
                AuditRelay::enqueue("{\"t\":\"wf/prb\",\"m\":\"00:00:00\",\"s\":\"UART_TEST\"}");
            AuditRelay::tick();
            canvas->clear();
            canvas->fillRect(0, 0, 128, 12, C_WHITE);
            canvas->drawText(2, 2, "记录中转", 8, true);
            canvas->drawText(2, 17, AuditRelay::status(), 8);
            char counts[48];
            snprintf(counts, sizeof(counts), "已交给主控:%u 丢弃:%u", AuditRelay::stored(),
                     AuditRelay::dropped());
            canvas->drawTextInRect(2, 30, 124, 10, counts, 0, 8);
            canvas->drawText(2, 43, "确认发送测试记录", 8);
            canvas->drawText(2, 55, "退出BW16后开蓝牙", 8);
            canvas->display();
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }
};

#endif
