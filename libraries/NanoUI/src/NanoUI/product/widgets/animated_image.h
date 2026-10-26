#ifndef WIDGETS_ANIMATED_IMAGE_H
#define WIDGETS_ANIMATED_IMAGE_H
#include "../framework/widget.h"
#include "../rendering/render_object.h"
#include "../hal/hal_time.h"

// 必须与 Python 端的打包格式一致
struct AssetHeader {
    uint16_t magic;   // 必须是 0x4E55 ('N', 'U')
    uint8_t version;  // 必须是 1
    uint8_t type;     // 1: 静态图, 2: 动画/视频
    uint16_t width;
    uint16_t height;
    uint16_t totalFrames;
    uint16_t fps;
};  // Size = 12 bytes

class RenderAnimatedImage : public RenderObject {
   private:
    const uint8_t* asset_data;
    bool loop;
    uint32_t startTime = 0;
    bool playing = true;

   public:
    RenderAnimatedImage(const uint8_t* data, bool loopPlay) : asset_data(data), loop(loopPlay) {
        startTime = hal_get_ticks();
    }

    void performLayout() override {
        if (!asset_data) {
            size = constraints.constrainSize({0, 0});
            return;
        }

        const AssetHeader* header = (const AssetHeader*)asset_data;
        if (header->magic != 0x4E55) {
            size = constraints.constrainSize({0, 0});
            return;
        }

        // 组件大小由视频/图像的实际物理分辨率决定
        if (header->width == 0 || header->height == 0) {
            size = constraints.constrainSize({0, 0});
            return;
        }
        size = constraints.constrainSize({(int16_t)header->width, (int16_t)header->height});
    }

    void paintWorker(Canvas* canvas, Point absPos) override {
        if (!asset_data || size.width == 0)
            return;
        const AssetHeader* header = (const AssetHeader*)asset_data;

        int frameIdx = 0;

        if (header->type == 2 && header->fps > 0 && header->totalFrames > 0) {
            // 播放视频/动画
            uint32_t now = hal_get_ticks();
            uint32_t elapsed = now - startTime;
            uint32_t frameDurationMs = 1000 / header->fps;
            if (frameDurationMs == 0)
                frameDurationMs = 1;

            frameIdx = elapsed / frameDurationMs;

            if (frameIdx >= header->totalFrames) {
                if (loop) {
                    frameIdx = frameIdx % header->totalFrames;
                } else {
                    frameIdx = header->totalFrames - 1;  // 停在最后一帧
                    playing = false;
                }
            }

            if (playing && WindowManager::instance) {
                // 只要还在播放，就持续触发 UI 引擎重绘下一帧
                WindowManager::instance->markDirty();
            }
        }

        // 计算当前帧的物理显存地址
        // 每帧字节数 = 宽 * 高(对齐到 8 的倍数)
        uint32_t pages = (header->height + 7) / 8;
        uint32_t bytesPerFrame = header->width * pages;

        // 数据起点 = 结构体大小(12) + 帧偏移
        const uint8_t* frameData = asset_data + 12 + (frameIdx * bytesPerFrame);

        // 使用垂直硬件取模的图像绘制函数。
        canvas->drawBitmapPage(absPos.x, absPos.y, frameData, header->width, header->height,
                               C_WHITE);
    }

    // --- 播放控制接口 ---
    void play() {
        startTime = hal_get_ticks();
        playing = true;
    }
    void stop() { playing = false; }
};

class AnimatedImage : public Widget {
   public:
    const uint8_t* asset_data;
    bool loop;

    AnimatedImage(const uint8_t* data, bool loopPlay = true) : asset_data(data), loop(loopPlay) {}

    RenderObject* createRenderObject() override {
        return new RenderAnimatedImage(asset_data, loop);
    }
};

#endif
