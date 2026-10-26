#ifndef APP_WS2812_CONTROL_H
#define APP_WS2812_CONTROL_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/widgets/controls.h"
#include "hw_ws2812.h"
#include "input_feedback.h"
#include <cstdio>
#include <vector>
#include <string>

extern "C" void USB_LOG(const char* format, ...);

class WS2812ControlApp : public Application {
private:
    static inline WS2812ControlApp* instance = nullptr;
    float r = 255.0f, g = 0.0f, b = 0.0f;
    float brightness = 100.0f;
    
    NanoUI::NanoUIText r_str, g_str, b_str, br_str;

    void updateStrings() {
        char buf[16];
        snprintf(buf, sizeof(buf), "%3d", (int)r); r_str = buf;
        snprintf(buf, sizeof(buf), "%3d", (int)g); g_str = buf;
        snprintf(buf, sizeof(buf), "%3d", (int)b); b_str = buf;
        snprintf(buf, sizeof(buf), "%3d%%", (int)brightness); br_str = buf;
    }

    static void onReset() {
        if (instance) {
            instance->r = 255; instance->g = 255; instance->b = 255; 
            instance->brightness = 100;
            instance->updateStrings();
            instance->applyToHardware();
        }
    }

    Widget* buildControlRow(const char* label, float& val, float max, NanoUI::NanoUIText* str) {
        return new Row(NanoUI::EmbeddedVector<Widget*>{
            new SizedBox(4, 0),
            new Container(new Text(label, 1), 20, 16, 0, false),
            new Slider(val, max),
            new SizedBox(4, 0),
            new Container(new SlidingText(str, 1), 30, 16, 0, false),
            new Spacer()
        });
    }

public:
    const char* getAppName() override { return "RGB PRO"; }

    void onCreate() override { 
        InputFeedback::setManual(true);
        instance = this;
        updateStrings();
        applyToHardware(true);
    }
    
    void onDestroy() override { 
        if (instance == this) instance = nullptr;
        led_drv.clear(); 
        led_drv.show();
        InputFeedback::setManual(false);
    }

    void applyToHardware(bool diagnose = false) {
        led_drv.setBrightness((uint8_t)brightness);
        for(int i=0; i<LED_COUNT; i++) {
            led_drv.setLED(i, (uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
        if (diagnose) {
            WS2812_Driver::Diagnostics d;
            USB_LOG("[LED] BEGIN\r\n");
            led_drv.show(&d);
            // Print after the stop attempt and IRQ re-enable, never in bit output.
            USB_LOG("[LED] hw=%lu start=%lu stop=%lu bright=%lu grb=%lu cr1=%lu ccer=%lu ccmr1=%lu psc=%lu arr=%lu cnta=%lu cntb=%lu clk=%lu mode=%lu af=%lu type=%lu\r\n",
                (unsigned long)d.supported, (unsigned long)d.start, (unsigned long)d.stop,
                (unsigned long)d.brightness, (unsigned long)d.grb,
                (unsigned long)d.cr1, (unsigned long)d.ccer, (unsigned long)d.ccmr1,
                (unsigned long)d.psc, (unsigned long)d.arr, (unsigned long)d.cnt_a,
                (unsigned long)d.cnt_b, (unsigned long)d.clock_enabled,
                (unsigned long)d.pin_mode, (unsigned long)d.pin_af, (unsigned long)d.pin_type);
        } else led_drv.show();
    }

    void onTick() override {
        static float lr=-1, lg=-1, lb=-1, lbr=-1;
        static uint32_t last_apply = 0;
        
        bool changed = (r != lr || g != lg || b != lb || brightness != lbr);
        
        // 限流：内容变化时最快每 50ms 刷新一次。
        if (changed && (HAL_GetTick() - last_apply > 50)) {
            updateStrings();
            applyToHardware();
            lr = r; lg = g; lb = b; lbr = brightness;
            last_apply = HAL_GetTick();
            if (WindowManager::instance) WindowManager::instance->markDirty();
        }
    }

    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> items;
        
        items.push_back(new SizedBox(0, 4));
        // items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{new Spacer(), new Text("RGB PRO CONTROL", 1), new Spacer()}));
        items.push_back(new Container(nullptr, 120, 1, 1, false, 0)); // Divider
        items.push_back(new SizedBox(0, 4));

        items.push_back(buildControlRow(" R", r, 255.0f, &r_str));
        items.push_back(buildControlRow(" G", g, 255.0f, &g_str));
        items.push_back(buildControlRow(" B", b, 255.0f, &b_str));
        items.push_back(buildControlRow(" L", brightness, 100.0f, &br_str));

        items.push_back(new SizedBox(0, 8));

        // 功能按钮
        items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{
            new Spacer(),
            new Button(new Container(new Text(" RESET ", 8), 0, 16, 1, true, 2), onReset),
            new SizedBox(10, 0),
            new Button(new Container(new Text(" EXIT ", 8), 0, 16, 1, true, 2), [](){
                AppManager::exitCurrentApp();
            }),
            new Spacer()
        }));

        items.push_back(new SizedBox(0, 8));

        return new Container(
            new ScrollColumn(items),
            128, 64, 0, false, 0
        );
    }
};

#endif
