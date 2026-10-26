#ifndef APP_TIME_SET_H
#define APP_TIME_SET_H

#include "NanoUI/NanoUI.h"
#include <stdio.h>
#include "rtc.h"

// Reuse NumberPicker input behavior, leaving the single rounded highlight to
// WindowManager. An underline distinguishes editing from ordinary focus.
class RenderTimePicker : public RenderNumberPicker {
   public:
    RenderTimePicker(int* value, int minimum, int maximum, const char* format)
        : RenderNumberPicker(value, minimum, maximum, format) {}
    int16_t getRadius() override { return 2; }
    void paintWorker(Canvas* canvas, Point p) override {
        char text[8];
        snprintf(text, sizeof(text), format, *value);
        const int width = get_utf8_string_width(text, 8);
        canvas->drawText(p.x + (size.width - width) / 2, p.y + 5, text, 8);
        if (isEditing)
            canvas->drawLine(p.x + 8, p.y + 15, p.x + size.width - 9, p.y + 15, C_WHITE);
    }
};
class TimePicker : public NumberPicker {
   public:
    TimePicker(int* value, int minimum, int maximum, const char* format)
        : NumberPicker(value, minimum, maximum, format) {}
    RenderObject* createRenderObject() override {
        return new RenderTimePicker(value, minVal, maxVal, format);
    }
};
class TimeSettingApp : public Application {
   private:
    static inline TimeSettingApp* instance = nullptr;

    int Y = 26, Mon = 1, D = 1, H = 12, M = 0;

    Widget* buildSettingRow(const char* label, Widget* picker) {
        NanoUI::EmbeddedVector<Widget*> rowCells;
        rowCells.push_back(new SizedBox(8, 0));
        rowCells.push_back(new ConstrainedBox(new Align(new Text(label, 8), Alignment::CenterLeft),
                                              BoxConstraints::tight(40, 18)));
        rowCells.push_back(new Spacer());
        rowCells.push_back(picker);
        rowCells.push_back(new SizedBox(8, 0));
        return new Row(rowCells);
    }

    static void onSave() {
        if (instance->Y < 0 || instance->Y > 99 || instance->Mon < 1 || instance->Mon > 12 ||
            instance->D < 1 || instance->H < 0 || instance->H > 23 || instance->M < 0 ||
            instance->M > 59) {
            Popup::showToast("日期无效");
            return;
        }
        const int year = 2000 + instance->Y;
        const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        const int maxDay = monthDays[instance->Mon - 1] + (instance->Mon == 2 && leap ? 1 : 0);
        if (instance->D > maxDay) {
            Popup::showToast("日期无效");
            return;
        }
        RTC_TimeTypeDef sTime = {0};
        RTC_DateTypeDef sDate = {0};
        sTime.Hours = instance->H;
        sTime.Minutes = instance->M;
        sTime.Seconds = 0;
        sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        sTime.StoreOperation = RTC_STOREOPERATION_RESET;

        sDate.Year = instance->Y;
        sDate.Month = instance->Mon;
        sDate.Date = instance->D;
        // Gregorian weekday, converted from Sunday=0 to STM32 Monday=1.
        static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        const int weekdayYear = year - (instance->Mon < 3 ? 1 : 0);
        const int weekday = (weekdayYear + weekdayYear / 4 - weekdayYear / 100 + weekdayYear / 400 +
                             offsets[instance->Mon - 1] + instance->D) %
                            7;
        sDate.WeekDay = weekday == 0 ? 7 : weekday;

        if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK ||
            HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
            Popup::showToast("时间保存失败");
        else
            Popup::showToast("时间已保存");
    }

    static void onExit() { AppManager::exitCurrentApp(); }

   public:
    const char* getAppName() override { return "TIME_SET"; }

    void onCreate() override {
        instance = this;
        RTC_TimeTypeDef sTime = {0};
        RTC_DateTypeDef sDate = {0};
        HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
        HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

        H = sTime.Hours;
        M = sTime.Minutes;
        Y = sDate.Year;
        Mon = sDate.Month;
        D = sDate.Date;
    }

    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> items;
        items.push_back(new SizedBox(0, 6));

        NanoUI::EmbeddedVector<Widget*> titleRow;
        titleRow.push_back(new Spacer());
        titleRow.push_back(new Text("时间设置", 8));
        titleRow.push_back(new Spacer());
        items.push_back(new Row(titleRow));

        items.push_back(new SizedBox(0, 6));
        items.push_back(buildSettingRow("年份", new TimePicker(&Y, 0, 99, "20%02d")));
        items.push_back(new SizedBox(0, 4));
        items.push_back(buildSettingRow("月份", new TimePicker(&Mon, 1, 12, "%02d")));
        items.push_back(new SizedBox(0, 4));
        items.push_back(buildSettingRow("日期", new TimePicker(&D, 1, 31, "%02d")));
        items.push_back(new SizedBox(0, 4));
        items.push_back(buildSettingRow("小时", new TimePicker(&H, 0, 23, "%02d")));
        items.push_back(new SizedBox(0, 4));
        items.push_back(buildSettingRow("分钟", new TimePicker(&M, 0, 59, "%02d")));

        items.push_back(new SizedBox(0, 12));

        NanoUI::EmbeddedVector<Widget*> footerBtns;
        footerBtns.push_back(new Spacer());
        footerBtns.push_back(
            new Button(new Container(new Text("保存", 8), 46, 16, 0, false, 2), onSave));
        footerBtns.push_back(new SizedBox(10, 0));
        footerBtns.push_back(
            new Button(new Container(new Text("返回", 8), 46, 16, 0, false, 2), onExit));
        footerBtns.push_back(new Spacer());

        items.push_back(new Row(footerBtns));
        items.push_back(new SizedBox(0, 8));

        return new Container(new ScrollColumn(items), 128, 64, 0, false, 0);
    }
};

#endif
