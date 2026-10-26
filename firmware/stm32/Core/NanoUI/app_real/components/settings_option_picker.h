#ifndef APP_REAL_SETTINGS_OPTION_PICKER_H
#define APP_REAL_SETTINGS_OPTION_PICKER_H

#include <NanoUI.h>
#include <cmath>
#include <cstring>

// Existing product text picker, kept outside the page's declarative composition.
// OK enters/leaves editing; directions change the draft only while editing.
class RenderSettingsOptionPicker : public RenderObject {
    int* index;
    NanoUI::StaticVector<const char*, 8> options;
    NanoUI::Callback<void()> onChanged;
    bool editing = false;
    float offset = 0;
    int previous;

   public:
    RenderSettingsOptionPicker(int* value, NanoUI::StaticVector<const char*, 8> choices,
                               NanoUI::Callback<void()> changed)
        : index(value), options(choices), onChanged(changed), previous(*value) {}

    bool canFocus() override { return true; }
    void performLayout() override { size = constraints.constrainSize({46, 16}); }

    bool handleEvent(Event event) override {
        if (!hasFocus)
            return false;
        if (event.type == EVT_OK) {
            editing = !editing;
            return true;
        }
        if (!editing)
            return false;
        int delta = 0;
        if (event.type == EVT_UP || event.type == EVT_RIGHT)
            delta = 1;
        else if (event.type == EVT_DOWN || event.type == EVT_LEFT)
            delta = -1;
        if (!delta)
            return false;
        previous = *index;
        *index = (*index + delta + static_cast<int>(options.size())) % options.size();
        offset = delta * 12.0f;
        if (onChanged)
            onChanged();
        return true;
    }

    void paintWorker(Canvas* canvas, Point p) override {
        if (std::fabs(offset) > 0.1f) {
            offset *= 0.6f;
            if (WindowManager::instance)
                WindowManager::instance->markDirty();
        }
        ScopedClip clip(canvas, {p.x, p.y, NanoCoordinate(static_cast<int32_t>(p.x) + size.width),
                                 NanoCoordinate(static_cast<int32_t>(p.y) + size.height)});
        if (editing)
            canvas->fillRoundRect(p.x, p.y, size.width, size.height, 3, C_WHITE);
        else
            canvas->drawRoundRect(p.x, p.y, size.width, size.height, 3, C_WHITE);
        const char* current = options[*index];
        int y = p.y + 4 + static_cast<int>(offset);
        canvas->drawText(p.x + (size.width - get_utf8_string_width(current, 8)) / 2, y, current, 8,
                         editing);
        if (std::fabs(offset) > 0.5f) {
            const char* old = options[previous];
            canvas->drawText(p.x + (size.width - get_utf8_string_width(old, 8)) / 2,
                             y + (offset > 0 ? -12 : 12), old, 8, editing);
        }
    }
};

class SettingsOptionPicker : public Widget {
    int* index;
    NanoUI::StaticVector<const char*, 8> options;
    NanoUI::Callback<void()> onChanged;

   public:
    SettingsOptionPicker(int* value, NanoUI::StaticVector<const char*, 8> choices,
                         NanoUI::Callback<void()> changed)
        : index(value), options(choices), onChanged(changed) {}
    RenderObject* createRenderObject() override {
        return new RenderSettingsOptionPicker(index, options, onChanged);
    }
};

#endif
