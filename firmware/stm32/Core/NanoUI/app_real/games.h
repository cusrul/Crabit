#ifndef APP_DUMMY_H
#define APP_DUMMY_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/sensor_manager.h"
#include "NanoUI/core/sensor_types.h"
#include <stdio.h>
#include <math.h>
#include "stm_sensor_service.h"

// =========================================================
// 馃幃 娓告垙 1锛欸RAVITY PONG (閲嶅姏涔掍箵 - 缁濆鏄犲皠鐗?
// =========================================================
class PongGameApp : public Application {
public:
    const char* getAppName() override { return "G_PONG"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        uint32_t last_frame = hal_get_ticks();
        
        float ball_x = 64.0f, ball_y = 32.0f;
        float ball_dx = 2.5f, ball_dy = 1.5f;
        float pad_left_y = 32.0f;  
        float pad_right_y = 32.0f; 
        int score_ai = 0, score_player = 0;
        
        while(running) {
            UIEvent e = input->poll();
            if(e.type == EVT_BACK) running = false; 

            uint32_t now = hal_get_ticks();
            if (now - last_frame >= 33) {
                last_frame = now;
                
                // 馃専 淇 2锛氭墜鑵曠粷瀵瑰Э鎬佹槧灏?(閲嶅姏鍙樹綅缃?
                StmSensorSnapshot snapshot{};
                if (STM32_GetUiSensorSnapshot(snapshot)) {
                    const auto* data = &snapshot.imu;
                    // 骞虫斁(ay=0)鎸℃澘鍦ㄤ腑闂?32)锛涘€炬枩鏃舵寜姣斾緥绉诲姩
                    pad_right_y = 32.0f + (data->ay * 60.0f); 
                }
                
                if (pad_left_y < ball_y - 4) pad_left_y += 1.5f;
                if (pad_left_y > ball_y + 4) pad_left_y -= 1.5f;

                if (pad_left_y < 8) pad_left_y = 8;   if (pad_left_y > 56) pad_left_y = 56;
                if (pad_right_y < 8) pad_right_y = 8; if (pad_right_y > 56) pad_right_y = 56;

                ball_x += ball_dx;
                ball_y += ball_dy;

                if (ball_y <= 0) { ball_y = 0; ball_dy = -ball_dy; }
                if (ball_y >= 63) { ball_y = 63; ball_dy = -ball_dy; }

                if (ball_x >= 118 && ball_x <= 122 && abs(ball_y - pad_right_y) <= 10) {
                    ball_x = 118;
                    ball_dx = -ball_dx * 1.05f; 
                    if (STM32_GetUiSensorSnapshot(snapshot)) {
                         const auto* data = &snapshot.imu;
                         ball_dy += data->ay * 2.0f; // 鎿﹁竟鍒囩悆
                    }
                }
                
                if (ball_x <= 10 && ball_x >= 6 && abs(ball_y - pad_left_y) <= 10) {
                    ball_x = 10;
                    ball_dx = -ball_dx;
                }

                if (ball_x < 0) { score_player++; ball_x = 64; ball_y = 32; ball_dx = 2.0f; }
                if (ball_x > 128) { score_ai++; ball_x = 64; ball_y = 32; ball_dx = -2.0f; }

                canvas->clear();
                
                for(int i = 0; i < 64; i += 4) canvas->drawPixel(64, i, C_WHITE);
                
                char buf[16];
                snprintf(buf, sizeof(buf), "%d  %d", score_ai, score_player);
                canvas->drawText(50, 4, buf, 1);

                canvas->fillRect(6, (int)pad_left_y - 8, 2, 16, C_WHITE);
                canvas->fillRect(120, (int)pad_right_y - 8, 2, 16, C_WHITE);
                canvas->fillRect((int)ball_x - 1, (int)ball_y - 1, 3, 3, C_WHITE);

                canvas->display();
            }
            
            // 馃専 淇 1锛氭晳鍛界殑 1 姣锛佽鍑?CPU 缁欏簳灞傜殑 SensorHub 浠诲姟鍘昏浼犳劅鍣?
            hal_delay(1);
        }
    }
};

// =========================================================
// 馃幃 娓告垙 2锛歀ASER ETCH (闃插悶鏁版嵁鐗?
// =========================================================
class EtchGameApp : public Application {
public:
    const char* getAppName() override { return "LASER_DRAW"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        uint32_t last_frame = hal_get_ticks();
        
        float cx = 64.0f, cy = 32.0f; 
        bool blink = true;
        
        // 馃専 淇 3锛氫娇鐢ㄧ粷瀵瑰潗鏍囨潵璁＄畻宸€?
        int last_px = 0, last_py = 0;
        bool first_read = true;
        
        canvas->clear();
        canvas->drawText(20, 26, "LASER READY", 1);
        canvas->display();
        hal_delay(1000);
        canvas->clear();

        while(running) {
            UIEvent e = input->poll();
            if(e.type == EVT_BACK) running = false; 
            if(e.type == EVT_OK) canvas->clear(); // 鐭寜鎸夐敭娓呯┖鐢绘澘

            uint32_t now = hal_get_ticks();
            if (now - last_frame >= 20) { 
                last_frame = now;
                blink = !blink;

                StmSensorSnapshot snapshot{};
                if (STM32_GetUiSensorSnapshot(snapshot)) {
                    const auto* pdata = &snapshot.optical;
                    
                    if (first_read) {
                        last_px = pdata->x; last_py = pdata->y;
                        first_read = false;
                    }
                    
                    if (!pdata->lifted) {
                        // 馃専 瀹岀編鑾峰彇宸€硷細鐢ㄥ綋鍓嶇殑缁濆鍧愭爣鍑忓幓涓婁竴甯х殑缁濆鍧愭爣
                        int delta_x = pdata->x - last_px;
                        int delta_y = pdata->y - last_py;
                        
                        cx += delta_x * 0.25f; // 婵€鍏夌伒鏁忓害寰皟
                        cy += delta_y * 0.25f;
                        
                        if (cx < 0) cx = 0; if (cx > 127) cx = 127;
                        if (cy < 0) cy = 0; if (cy > 63) cy = 63;
                        
                        canvas->drawPixel((int)cx, (int)cy, C_WHITE);
                    }
                    
                    // 璁板綍鏈€鏂扮殑缁濆鍧愭爣
                    last_px = pdata->x; 
                    last_py = pdata->y;
                }

                if (blink) {
                    canvas->drawPixel((int)cx - 2, (int)cy, C_WHITE);
                    canvas->drawPixel((int)cx + 2, (int)cy, C_WHITE);
                    canvas->drawPixel((int)cx, (int)cy - 2, C_WHITE);
                    canvas->drawPixel((int)cx, (int)cy + 2, C_WHITE);
                }

                canvas->display();
            }
            
            // 馃専 淇 1锛氭晳鍛界殑寤舵椂锛?
            hal_delay(1);
        }
    }
};

class GameApp : public PongGameApp {}; // 鍏煎浣犱箣鍓嶇殑璋冪敤

#endif

