// Include-only Evasion body; .h suffix preserves Arduino staging without
// auto-compilation.
#include "MyArduboy2.h"

void MyArduboy2::beginNoLogo(void)
{
    clear();
    setCursor(0, 0);
    setTextSize(1);
    setTextWrap(true);
    setTextColor(WHITE, BLACK);
    lastButtonState = currentButtonState = 0;
}

bool MyArduboy2::nextFrame(void)
{
    bool ret = Arduboy2Compat::nextFrame();
    if (ret) {
        lastButtonState = currentButtonState;
        currentButtonState = buttonsState();
    }
    return ret;
}

bool MyArduboy2::buttonDown(uint8_t buttons)
{
    return currentButtonState & ~lastButtonState & buttons;
}

bool MyArduboy2::buttonPressed(uint8_t buttons)
{
    return currentButtonState & buttons;
}

bool MyArduboy2::buttonUp(uint8_t buttons)
{
    return ~currentButtonState & lastButtonState & buttons;
}
