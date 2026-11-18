// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "MyArduboy.h"

void MyArduboy::beginNoLogo(void)
{
    clear();
    setCursor(0, 0);
    setTextSize(1);
    setTextWrap(true);
    setTextColor(WHITE, BLACK);
    lastButtonState = currentButtonState = 0;
}

bool MyArduboy::nextFrame(void)
{
    bool ret = Arduboy2Compat::nextFrame();
    if (ret) {
        lastButtonState = currentButtonState;
        currentButtonState = buttonsState();
    }
    return ret;
}

bool MyArduboy::buttonDown(uint8_t buttons)
{
    return currentButtonState & ~lastButtonState & buttons;
}

bool MyArduboy::buttonPressed(uint8_t buttons)
{
    return currentButtonState & buttons;
}

bool MyArduboy::buttonUp(uint8_t buttons)
{
    return ~currentButtonState & lastButtonState & buttons;
}
