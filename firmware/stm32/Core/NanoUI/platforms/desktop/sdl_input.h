#ifndef SDL_INPUT_H
#define SDL_INPUT_H

#include "NanoUI/hal/input.h"
#include <SDL2/SDL.h>
#include <queue>
#include <cstdio>

class SDLInputDriver : public InputDriver {
private:
    std::queue<UIEvent> eventQueue;
    bool traceEvents = false;

public:
    void init() override {}
    void setEventTracing(bool enabled) { traceEvents = enabled; }

    // 核心：供 WindowManager 轮询
    UIEvent poll() override {
        SDL_Event sdl_e;
        while (SDL_PollEvent(&sdl_e)) {
            if (sdl_e.type == SDL_QUIT) exit(0);
            this->injectSDLEvent(sdl_e); // 把事件压入你自己的内部队列
        }
        if (eventQueue.empty()) {
            return {EVT_NONE, 0};
        }
        UIEvent e = eventQueue.front();
        eventQueue.pop();
        return e;
    }

    // 供外部 SDL 循环注入事件
    void injectSDLEvent(SDL_Event& e) {
        if (traceEvents) {
            // Event kinds only: no typed text, credentials or user payload.
            std::printf("[SDL EVENT] %u\n", unsigned(e.type));
            std::fflush(stdout);
        }
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_UP:     eventQueue.push({EVT_UP, 0}); break;
                case SDLK_DOWN:   eventQueue.push({EVT_DOWN, 0}); break;
                case SDLK_RETURN: eventQueue.push({EVT_OK, 0}); break;
                case SDLK_ESCAPE: eventQueue.push({EVT_BACK, 0}); break;
                case SDLK_LEFT:   eventQueue.push({EVT_LEFT, 0}); break;
                case SDLK_RIGHT:  eventQueue.push({EVT_RIGHT, 0}); break;
            }
        }
    }
};
#endif
