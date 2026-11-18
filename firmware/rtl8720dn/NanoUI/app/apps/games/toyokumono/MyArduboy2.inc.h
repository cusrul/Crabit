// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "MyArduboy2.h"

/*  Local Constants  */

PROGMEM static const uint32_t imgFont[] = {
    0x00000000, 0x00017000, 0x000C00C0, 0x0A7CA7CA, 0x0855F542, 0x19484253, 0x1251F55E, 0x00003000,
    0x00452700, 0x001C9440, 0x0519F314, 0x0411F104, 0x00000420, 0x04104104, 0x00000400, 0x01084210,
    0x0F45145E, 0x0001F040, 0x13555559, 0x0D5D5551, 0x087C928C, 0x0D555557, 0x0D55555E, 0x010C5251,
    0x0F55555E, 0x0F555556, 0x0000A000, 0x0000A400, 0x0028C200, 0x0028A280, 0x00086280, 0x000D5040,
    0x0755745E, 0x1F24929C, 0x0D5D555F, 0x1145149C, 0x0725145F, 0x1155555F, 0x0114515F, 0x1D55545E,
    0x1F10411F, 0x0045F440, 0x07210410, 0x1D18411F, 0x1041041F, 0x1F04F05E, 0x1F04109C, 0x0F45545E,
    0x0314925F, 0x1F45D45E, 0x1B34925F, 0x0D555556, 0x0105F041, 0x0721041F, 0x0108421F, 0x0F41E41F,
    0x1D184317, 0x0109C107, 0x114D5651, 0x0045F000, 0x0001F000, 0x0001F440, 0x000C1080, 0x10410410,
};

/*---------------------------------------------------------------------------*/
/*                                 Initialize                                */
/*---------------------------------------------------------------------------*/

void MyArduboy2::beginNoLogo(void)
{
    clear();
    cursor_x = cursor_y = 0;
    textSize = 1;
    textWrap = true;
    textcolor = WHITE;
    textbg = BLACK;
    audio.begin();
    lastButtonState = currentButtonState = 0;
}

/*---------------------------------------------------------------------------*/
/*                               Buttons status                              */
/*---------------------------------------------------------------------------*/

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

/*---------------------------------------------------------------------------*/
/*                                Draw letters                               */
/*---------------------------------------------------------------------------*/

void MyArduboy2::setTextColors(uint8_t fgColor, uint8_t bgColor)
{
    textcolor = fgColor;
    textbg = bgColor;
}

size_t MyArduboy2::write(uint8_t c)
{
    if (c == '\n') {
        cursor_y += textSize * 6;
        cursor_x = 0;
    } else if (c >= ' ' && c <= '_') {
        myDrawChar(cursor_x, cursor_y, c, textcolor, textbg, textSize);
        cursor_x += textSize * 6;
        if (textWrap && (cursor_x > (WIDTH - textSize * 6))) write('\n');
    }
    return 1; // temporary
}

void MyArduboy2::myDrawChar(int16_t x, int16_t y, unsigned char c, uint8_t color,
                            uint8_t bg, uint8_t size)
{
    bool draw_bg = bg != color;

    if (x >= WIDTH || y >= HEIGHT || x + 5 * size < 0 || y + 6 * size < 0) return;
    uint32_t ptn = pgm_read_dword(imgFont + (c - ' '));
    for (int8_t i = 0; i < 6; i++) {
        for (int8_t j = 0; j < 6; j++) {
            bool draw_fg = ptn & 0x1;
            if (draw_fg || draw_bg) {
                if (size == 1) {
                    drawPixel(x + i, y + j, (draw_fg) ? color : bg);
                } else {
                    fillRect(x + i * size, y + j * size, size, size,
                             (draw_fg) ? color : bg);
                }
            }
            ptn >>= 1;
        }
    }
}
