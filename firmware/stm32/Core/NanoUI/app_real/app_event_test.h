#ifndef APP_EVENT_TEST_H
#define APP_EVENT_TEST_H

#include "NanoUI/NanoUI.h"
#include "global_store.h"
#include "event_bus.h"
#include "main.h" 
#include <vector>
#include <string.h>
#include <stdio.h>

extern volatile uint32_t dbg_rx_evts;
extern volatile uint32_t dbg_rx_errs;

class EventTestApp : public Application {
private:
    char header_str[32] = "BLE: IDLE";
    char tick_str[32] = "R:0 E:0"; 
    char log_raw[3][128] = {"", "", ""}; 
    char log_disp[3][22] = {"", "", ""}; 
    char last_msg_cache[64] = {0};
    
    uint32_t last_rx_evts = 0xFFFFFFFF; 
    uint32_t last_rx_errs = 0xFFFFFFFF;
    int scroll_pos[3] = {0, 0, 0}; 
    uint32_t last_scroll_tick = 0;

    void push_log(const char* msg) {
        strcpy(log_raw[0], log_raw[1]);
        strcpy(log_raw[1], log_raw[2]);
        snprintf(log_raw[2], 128, ">%s", msg);
        scroll_pos[0] = scroll_pos[1]; scroll_pos[1] = scroll_pos[2]; scroll_pos[2] = -6; 
    }

public:
    const char* getAppName() override { return "MODEM_DIAG"; }

    Widget* buildUI() override {
        snprintf(tick_str, sizeof(tick_str), "R:%lu E:%lu", dbg_rx_evts, dbg_rx_errs);
        snprintf(header_str, sizeof(header_str), "BLE:%s", strlen(GlobalStore.ble_status) > 0 ? GlobalStore.ble_status : "IDLE");

        NanoUI::EmbeddedVector<Widget*> items;
        items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{new Text(header_str, 1), new Spacer(), new Text(tick_str, 1)}));
        items.push_back(new SizedBox(0, 2));
        items.push_back(new Container(new SizedBox(128, 1), 128, 1, 0, true, 0)); 
        items.push_back(new SizedBox(0, 4));
        
        items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{new SizedBox(2, 0), new Text(log_disp[0], 1), new Spacer()}));
        items.push_back(new SizedBox(0, 2));
        items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{new SizedBox(2, 0), new Text(log_disp[1], 1), new Spacer()}));
        items.push_back(new SizedBox(0, 2));
        items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{new SizedBox(2, 0), new Text(log_disp[2], 1), new Spacer()}));

        items.push_back(new SizedBox(0, 6));
        
        NanoUI::EmbeddedVector<Widget*> btn_row;
        btn_row.push_back(new Button(new Container(new Text("PING", 8), 40, 14, 0, true, 2), []() {
            EventBus::publish("sys/bw/ping", "1");
            strcpy(GlobalStore.latest_notify, "TX: PING->BW16");
            if (WindowManager::instance) WindowManager::instance->markDirty();
        }));
        btn_row.push_back(new Spacer());
        
        btn_row.push_back(new Button(new Container(new Text("CLEAR", 8), 46, 14, 0, true, 2), []() {
            strcpy(GlobalStore.latest_notify, "CMD_CLEAR");
        }));
        
        items.push_back(new Row(btn_row));
        return new Container(new ScrollColumn(items), 128, 64, 0, false, 0);
    }

    void onTick() override {
        bool changed = false;

        if (dbg_rx_evts != last_rx_evts || dbg_rx_errs != last_rx_errs) {
            last_rx_evts = dbg_rx_evts; last_rx_errs = dbg_rx_errs;
            snprintf(tick_str, sizeof(tick_str), "R:%lu E:%lu", last_rx_evts, last_rx_errs);
            changed = true;
        }

        char current_ble[16];
        strncpy(current_ble, GlobalStore.ble_status, 15); current_ble[15] = '\0';
        if (strlen(current_ble) == 0) strcpy(current_ble, "IDLE");

        if (strncmp(header_str + 4, current_ble, 15) != 0) { 
            snprintf(header_str, sizeof(header_str), "BLE:%s", current_ble); changed = true;
        }

        if (strcmp(GlobalStore.latest_notify, "CMD_CLEAR") == 0) {
            for(int i=0; i<3; i++) { log_raw[i][0] = '\0'; log_disp[i][0] = '\0'; scroll_pos[i] = 0; }
            GlobalStore.latest_notify[0] = '\0'; last_msg_cache[0] = '\0'; changed = true;
        } else if (strcmp(last_msg_cache, GlobalStore.latest_notify) != 0 && strlen(GlobalStore.latest_notify) > 0) {
            strncpy(last_msg_cache, GlobalStore.latest_notify, 63); last_msg_cache[63] = '\0';
            push_log(last_msg_cache); changed = true;
        }

        uint32_t now = HAL_GetTick();
        if (now - last_scroll_tick > 80) { 
            last_scroll_tick = now;
            for (int i = 0; i < 3; i++) {
                int len = strlen(log_raw[i]);
                if (len > 21) {
                    scroll_pos[i]++;
                    int max_scroll = len - 21;
                    if (scroll_pos[i] > max_scroll + 6) scroll_pos[i] = -6;
                    int read_pos = scroll_pos[i] < 0 ? 0 : (scroll_pos[i] > max_scroll ? max_scroll : scroll_pos[i]);
                    strncpy(log_disp[i], log_raw[i] + read_pos, 21); log_disp[i][21] = '\0';
                    changed = true;
                } else if (strcmp(log_disp[i], log_raw[i]) != 0) {
                    strncpy(log_disp[i], log_raw[i], 21); log_disp[i][21] = '\0'; changed = true;
                }
            }
        }
        if (changed && WindowManager::instance) WindowManager::instance->markDirty();
    }
};
#endif

