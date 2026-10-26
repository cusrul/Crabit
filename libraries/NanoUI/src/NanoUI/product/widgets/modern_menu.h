#ifndef WIDGETS_MODERN_MENU_H
#define WIDGETS_MODERN_MENU_H

#include "carousel.h"
#include "basic.h"
#include "flex.h"
#include "container.h"
#include "image.h"
#include "button.h"
#include "text.h"
#include "animated_image.h"  // 提供 MenuIconAsset

// =========================================================
// DashedDivider：虚线分隔线。
// =========================================================
class RenderDashedDivider : public RenderObject {
   public:
    void performLayout() override { size = constraints.constrainSize({constraints.maxWidth, 1}); }
    void paintWorker(Canvas* canvas, Point p) override {
        for (int16_t x = 12; x < size.width - 12; x += 8) {
            canvas->drawLine(p.x + x, p.y, p.x + x + 4, p.y, C_WHITE);
        }
    }
};
class DashedDivider : public Widget {
   public:
    RenderObject* createRenderObject() override { return new RenderDashedDivider(); }
};

// =========================================================
// ModernDetailView：随菜单选中项联动的详情区。
// =========================================================
class RenderModernDetailView : public RenderObject {
   public:
    NanoUITitleList titles;
    NanoUI::StateKey stateKey;
    uint8_t titleSize;
    int lastIndex = -1;
    SpringAnimator barAnim, textAnim;

    template <typename Container>
    RenderModernDetailView(const Container& t, NanoUI::StateKey key, uint8_t textSize = 12)
        : titles(t.begin(), t.end()),
          stateKey(key),
          titleSize(textSize),
          barAnim(0, 0.2f, 0.6f),
          textAnim(15.0f, 0.15f, 0.7f) {
        lastIndex = -2;
    }

    void performLayout() override { size = constraints.constrainSize({constraints.maxWidth, 20}); }

    void paintWorker(Canvas* canvas, Point p) override {
        if (titles.empty() || !WindowManager::instance)
            return;
        int curr = WindowManager::instance->getState(stateKey, 0);
        if (curr != lastIndex) {
            lastIndex = curr;
            barAnim.jumpTo(0);
            barAnim.set(3);
            textAnim.jumpTo(15);
            textAnim.set(0);
        }
        float bw = barAnim.update();
        float ty = textAnim.update();
        if (!barAnim.isStable() || !textAnim.isStable())
            WindowManager::instance->markDirty();
        if (curr >= 0 && curr < (int)titles.size()) {
            if (bw > 0.5f)
                canvas->fillRect(p.x + 8, p.y + 2, (int16_t)bw, 12, C_WHITE);
            ScopedClip clip(canvas, {NanoCoordinate(static_cast<int32_t>(p.x) + 16), p.y,
                                     NanoCoordinate(static_cast<int32_t>(p.x) + size.width - 8),
                                     NanoCoordinate(static_cast<int32_t>(p.y) +
                                                    NanoMin<int32_t>(16, size.height))});
            canvas->drawText(p.x + 16, p.y + (16 - titleSize) / 2 + (int16_t)ty,
                             titles[curr].c_str(), titleSize, false, 0);
        }
    }
};

class ModernDetailView : public Widget {
   public:
    NanoUITitleList titles;
    NanoUI::StateKey stateKey;
    uint8_t titleSize;
    template <typename Container>
    ModernDetailView(const Container& t, NanoUI::StateKey key, uint8_t textSize = 12)
        : titles(t.begin(), t.end()), stateKey(key), titleSize(textSize) {}
    RenderObject* createRenderObject() override {
        return new RenderModernDetailView(titles, stateKey, titleSize);
    }
};

// =========================================================
// ModernMenu：图标、虚线与文字联动的复合菜单。
// =========================================================
class ModernMenu : public SingleChildWidget {
   public:
    ModernMenu(std::initializer_list<Widget*> items, std::initializer_list<NanoUITitle> titles,
               const char* key, uint8_t titleSize = 12)
        : ModernMenu(NanoUI::EmbeddedVector<Widget*>(items), NanoUITitleList(titles),
                     NanoUI::makeStateKey(key), titleSize) {}

    template <typename WidgetContainer, typename TitleContainer>
    ModernMenu(const WidgetContainer& items, const TitleContainer& titles, NanoUI::StateKey key,
               uint8_t titleSize = 12)
        : SingleChildWidget(nullptr) {
        child = new ClipRect(
            new Align(new Column({new Spacer(), new Carousel(items, NanoUITitleList{}, 14, key),
                                  new SizedBox(0, 0), new DashedDivider(), new SizedBox(0, 4),
                                  new ModernDetailView(titles, key, titleSize), new Spacer()}),
                      Alignment::TopLeft, true, true));
    }

    template <typename WidgetContainer, typename TitleContainer>
    ModernMenu(const WidgetContainer& items, const TitleContainer& titles, const char* key,
               uint8_t titleSize = 12)
        : ModernMenu(items, titles, NanoUI::makeStateKey(key), titleSize) {}

    RenderObject* createRenderObject() override {
        return child ? child->createRenderObject() : nullptr;
    }
};

// 快捷图标辅助函数：原始点阵版 16x16。
inline Widget* MenuIcon(const uint8_t* icon, VoidCallback onClick) {
    return new Button(new Container(new Row({new Spacer(), new Image(icon, 16, 16), new Spacer()}),
                                    28, 28, 0, false, 4),
                      onClick);
}

// 快捷图标辅助函数：资产包版，自动识别宽高与视频。
inline Widget* MenuIconAsset(const uint8_t* asset, VoidCallback onClick) {
    return new Button(
        new Container(new Row({new Spacer(), new AnimatedImage(asset, true), new Spacer()}), 28, 28,
                      0, false, 4),
        onClick);
}

#endif
