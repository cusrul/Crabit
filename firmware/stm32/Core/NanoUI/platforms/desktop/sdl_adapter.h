#ifndef SDL_ADAPTER_H
#define SDL_ADAPTER_H

#include "NanoUI/hal/canvas.h"
#include <SDL2/SDL.h>
#include <string>
#include <cstdio>
#include "hal_mock.h" 

class SDLAdapter : public Canvas {
private:
  SDL_Renderer *renderer;
  int w, h;
  uint8_t *buffer; // 逻辑显存
  const char* snapshotPath = nullptr;

public:
  SDLAdapter(SDL_Renderer *r, int width, int height)
      : renderer(r), w(width), h(height) {
      buffer = new uint8_t[width * height];
      memset(buffer, 0, width * height);
  }

  ~SDLAdapter() {
      delete[] buffer;
  }

  // --- 基础系统接口 ---
  void clear() override {
    memset(buffer, 0, w * h);
  }

  void display() override { 
    SDL_RenderSetClipRect(renderer, NULL);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (buffer[y * w + x]) SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            else SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderDrawPoint(renderer, x, y);
        }
    }
    SDL_RenderPresent(renderer); 
    if (snapshotPath) saveSnapshot(snapshotPath);
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    if (color == C_WHITE) buffer[y * w + x] = 1;
    else if (color == C_BLACK) buffer[y * w + x] = 0;
    else if (color == C_INVERT) buffer[y * w + x] = !buffer[y * w + x];
  }

  int16_t width() override { return w; }
  int16_t height() override { return h; }
  void setSnapshotPath(const char* path) { snapshotPath = path; }

  // Desktop-only diagnostic. Captures the actual logical framebuffer, not a
  // second rendering of the App. No file is written unless explicitly asked.
  bool saveSnapshot(const char* path) const {
    const unsigned sw = w * 4, sh = h * 4, bytes = 54 + sw * sh * 3;
    uint8_t header[54]{};
    header[0] = 'B'; header[1] = 'M'; header[10] = 54; header[14] = 40;
    for (int i = 0; i < 4; ++i) {
      header[2+i] = (bytes >> (8*i)) & 255;
      header[18+i] = (sw >> (8*i)) & 255;
      header[22+i] = (sh >> (8*i)) & 255;
    }
    header[26] = 1; header[28] = 24;
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    bool ok = std::fwrite(header, 1, sizeof header, f) == sizeof header;
    for (int y = sh - 1; y >= 0; --y) for (unsigned x = 0; x < sw; ++x) {
      const uint8_t value = buffer[(y / 4) * w + x / 4] ? 255 : 0;
      const uint8_t rgb[] = {value, value, value};
      ok = std::fwrite(rgb, 1, 3, f) == 3 && ok;
    }
    return std::fclose(f) == 0 && ok;
  }

  // 移除所有绘图覆盖，由基类 Canvas 通过 drawPixel 实现，确保 C_INVERT 逻辑统一
};

#endif
