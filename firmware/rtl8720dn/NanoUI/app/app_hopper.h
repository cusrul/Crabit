#ifndef APP_HOPPER_H
#define APP_HOPPER_H

#include "../src/NanoUI.h"
#include "../AmebaStore.h"
#include <math.h>
#include <vector>

// ==========================================================
// Obono Hopper: NanoUI Native Remake
// ==========================================================

class AppHopper : public Application {
private:
    // 使用 HOPPER_ 前缀，避免与 Castle Boy 宏冲突
    enum GameState { HOPPER_STATE_TITLE, HOPPER_STATE_GAME, HOPPER_STATE_OVER };
    GameState state = HOPPER_STATE_TITLE;

    // 核心参数
    static const int CENTER_X = 64;
    static const int CENTER_Y = 32;
    static const int FLOOR_Z_MAX = 4096;
    static const int NUM_FLOORS = 10;

    // 玩家像素帧 (16个旋转角度)
    const uint8_t imgPlayers[16][8] = {
        { 0x66, 0x24, 0x3C, 0xFF, 0x3C, 0x5A, 0x5A, 0x3C }, { 0x30, 0x13, 0xFD, 0x3C, 0x5A, 0xB5, 0xF4, 0x38 },
        { 0x0C, 0x44, 0x3F, 0x5D, 0xBC, 0xF4, 0x6A, 0x30 }, { 0x26, 0x12, 0x6C, 0x9C, 0xFF, 0xED, 0x54, 0x64 },
        { 0x08, 0x69, 0x9F, 0xFC, 0xFC, 0x9F, 0x69, 0x08 }, { 0x64, 0x54, 0xED, 0xFF, 0x9C, 0x6C, 0x12, 0x26 },
        { 0x30, 0x6A, 0xF4, 0xBC, 0x5D, 0x3F, 0x44, 0x0C }, { 0x38, 0xF4, 0xB5, 0x5A, 0x3C, 0xFD, 0x13, 0x30 },
        { 0x3C, 0x5A, 0x5A, 0x3C, 0xFF, 0x3C, 0x24, 0x66 }, { 0x1C, 0x2F, 0xAD, 0x5A, 0x3C, 0xBF, 0xC8, 0x0C },
        { 0x0C, 0x56, 0x2F, 0x3D, 0xBA, 0xFC, 0x22, 0x30 }, { 0x26, 0x2A, 0xB7, 0xFF, 0x39, 0x36, 0x48, 0x64 },
        { 0x10, 0x96, 0xF9, 0x3F, 0x3F, 0xF9, 0x96, 0x10 }, { 0x64, 0x48, 0x36, 0x39, 0xFF, 0xB7, 0x2A, 0x26 },
        { 0x30, 0x22, 0xFC, 0xBA, 0x3D, 0x2F, 0x56, 0x0C }, { 0x0C, 0xC8, 0xBF, 0x3C, 0x5A, 0xAD, 0x2F, 0x1C }
    };

    // 游戏变量
    int score = 0;
    int hiscore = 0;
    int level = 0;
    
    int playerX = 0, playerY = 0;
    int playerJump = 0, playerTorque = 0;
    int playerRotate = 196;
    
    struct Floor {
        bool active;
        int x, y, z;
        int vx, vy;
        int size;
    } floors[NUM_FLOORS];
    
    int floorIdxFirst = 0, floorIdxLast = 0;

    // ==========================================
    // 核心物理引擎
    // ==========================================
    void initGame() {
        score = 0;
        level = 0;
        playerX = 0;
        playerY = 0;
        playerJump = 64; 
        playerRotate = 196;
        
        for (int i = 0; i < NUM_FLOORS; i++) floors[i].active = false;
        
        floorIdxFirst = 0;
        floorIdxLast = 0;
        addFloor(0, 0, FLOOR_Z_MAX);
        
        state = HOPPER_STATE_GAME;
    }

    void addFloor(int x, int y, int z) {
        Floor& f = floors[floorIdxLast];
        f.active = true;
        f.x = x; f.y = y; f.z = z;
        
        double vd = (rand() % 256) * 3.14159 / 128.0;
        int vr = (rand() % 2) ? (256 + level * 16) : 0; // 50%概率产生移动地板
        f.vx = cos(vd) * vr;
        f.vy = sin(vd) * vr;
        
        f.size = 64 - level * 3;
        if (f.size < 16) f.size = 16;
        
        floorIdxLast = (floorIdxLast + 1) % NUM_FLOORS;
    }

    void updatePhysics(uint8_t raw_btns) {
        // 1. 玩家输入控制
        int vx = 0, vy = 0;
        if (raw_btns & (1 << 2)) vx--; // LEFT
        if (raw_btns & (1 << 3)) vx++; // RIGHT
        if (raw_btns & (1 << 0)) vy--; // UP
        if (raw_btns & (1 << 1)) vy++; // DOWN
        
        int vr = (vx != 0 && vy != 0) ? 384 : 512;
        playerX += vx * vr;
        playerY += vy * vr;

        if (vx != 0 || vy != 0) {
            int targetRotate = (int)(atan2(vy, vx) * 128.0 / 3.14159);
            int diffRotate = targetRotate - playerRotate;
            playerTorque = (diffRotate > 0) ? 2 : -2;
        }
        playerRotate += playerTorque;

        // 2. 视角滚动逻辑
        int scrollX = -playerX / 4;
        int scrollY = -playerY / 4;
        playerX += scrollX;
        playerY += scrollY;
        
        if (playerJump > -64) playerJump--;

        // 3. 地板移动与碰撞检测
        for (int i = 0; i < NUM_FLOORS; i++) {
            Floor& f = floors[i];
            if (!f.active) continue;
            
            f.x += scrollX + f.vx;
            f.y += scrollY + f.vy;
            f.z += playerJump;

            // 如果地板刚好穿过屏幕 (Z=0)，且玩家正在下落，检测是否接住
            if (playerJump <= 0 && f.z >= 0 && f.z + playerJump - 1 < 0) {
                int diff = std::max(abs(f.x - playerX), abs(f.y - playerY));
                if (diff < f.size * 128) {
                    // 接住了！反弹！
                    playerJump = 64;
                    score += 10;
                    level = score / 50; 
                    playerTorque = (rand() % 2 == 0 ? 1 : -1) * 8;
                }
            }
        }

        // 4. 清理飞到脑后的地板
        Floor& firstF = floors[floorIdxFirst];
        if (firstF.active && firstF.z > FLOOR_Z_MAX) {
            firstF.active = false;
            floorIdxFirst = (floorIdxFirst + 1) % NUM_FLOORS;
        }

        // 5. 生成新地板
        Floor& lastF = floors[(floorIdxLast - 1 + NUM_FLOORS) % NUM_FLOORS];
        if (state == HOPPER_STATE_GAME && lastF.z >= 0) {
            addFloor((rand() % 3 - 1) * 200, (rand() % 3 - 1) * 200, lastF.z - (512 + level * 32));
        }

        // 6. 死亡判定
        if (state == HOPPER_STATE_GAME && floors[floorIdxFirst].z < 0 && playerJump < -30) {
            state = HOPPER_STATE_OVER;
            if (score > hiscore) hiscore = score;
        }
    }

    // ==========================================
    // 渲染引擎
    // ==========================================
    void drawArduboySprite(Canvas* canvas, int x, int y, const uint8_t* bitmap, int w, int h) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                uint8_t byte = bitmap[(j / 8) * w + i];
                if (byte & (1 << (j % 8))) {
                    canvas->drawPixel(x + i, y + j, C_WHITE);
                }
            }
        }
    }

    void renderGame(Canvas* canvas) {
        // 1. 绘制伪 3D 地板
        for (int i = 0; i < NUM_FLOORS; i++) {
            int idx = (floorIdxFirst + i) % NUM_FLOORS;
            Floor& f = floors[idx];
            if (!f.active || f.z < 0 || f.size == 0) continue;
            
            // 核心透视投影算法
            int q = 256 + f.z;
            int s = f.size * 256 / q;
            int x = f.x / q + CENTER_X - s / 2;
            int y = f.y / q + CENTER_Y - s / 2;
            
            // 绘制地板外框与内部网格线(交替模拟图案)
            canvas->drawRect(x, y, s, s, C_WHITE);
            if (idx % 2 == 0) {
                canvas->drawLine(x, y, x + s, y + s, C_WHITE);
                canvas->drawLine(x + s, y, x, y + s, C_WHITE);
            }
        }

        // 2. 绘制旋转的玩家
        int px = playerX / 256 + CENTER_X - 4;
        int py = playerY / 256 + CENTER_Y - 4;
        int rotateIdx = (playerRotate + 8) / 16 % 16;
        if (rotateIdx < 0) rotateIdx += 16;
        
        // 玩家背光遮罩
        canvas->fillRect(px, py, 8, 8, C_BLACK);
        drawArduboySprite(canvas, px, py, imgPlayers[rotateIdx], 8, 8);

        // 3. HUD
        char buf[32];
        snprintf(buf, sizeof(buf), "分:%d", score);
        canvas->drawText(0, 0, buf, 12);
        snprintf(buf, sizeof(buf), "级:%d", level);
        canvas->drawText(88, 0, buf, 12);
    }

public:
    const char* getAppName() override { return "立体跳跃"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        uint32_t last_frame_time = hal_get_ticks();
        state = HOPPER_STATE_TITLE;

        while(running && is_display_active) {
            UIEvent e = input->poll();
            uint8_t raw_btns = AmebaStore::raw_btn_mask; // 直接取原生按键掩码
            
            // 全局退出逻辑
            if (e.type == EVT_BACK) {
                running = false;
                break;
            }

            // 60FPS 帧率控制锁
            uint32_t current_time = hal_get_ticks();
            if (current_time - last_frame_time < 16) {
                safe_delay(1);
                continue;
            }
            last_frame_time = current_time;

            canvas->clear();

            if (state == HOPPER_STATE_TITLE) {
                canvas->drawText(40, 16, "立体跳跃", 12);
                canvas->drawText(28, 40, "按确认键开始", 12);
                if (e.type == EVT_OK || (raw_btns & (1<<4))) {
                    initGame();
                }
            } 
            else if (state == HOPPER_STATE_GAME) {
                updatePhysics(raw_btns);
                renderGame(canvas);
            } 
            else if (state == HOPPER_STATE_OVER) {
                renderGame(canvas);
                canvas->fillRect(20, 18, 88, 32, C_BLACK);
                canvas->drawRect(20, 18, 88, 32, C_WHITE);
                canvas->drawText(40, 21, "游戏结束", 12);
                
                char buf[32];
                snprintf(buf, sizeof(buf), "最高:%d", hiscore);
                canvas->drawText(36, 35, buf, 12);
                
                if (e.type == EVT_OK || (raw_btns & (1<<4))) {
                    state = HOPPER_STATE_TITLE;
                }
            }

            canvas->display();
        }
    }
};

#endif
