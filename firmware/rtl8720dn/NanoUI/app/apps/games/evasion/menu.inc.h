#include "common.h"

/*  Defines  */

#define IMG_SOUND_W     16
#define IMG_SOUNDM_W    6
#define MENU_COUNT_MAX  6

/*  Typedefs  */

typedef struct
{
    void (*func)(void);
    const __FlashStringHelper *label;
} ITEM_T;

/*  Local Variables  */

PROGMEM static const uint8_t imgSound[] = { // 16x8
    0x00, 0x7C, 0x8E, 0xD6, 0xDA, 0xDA, 0x82, 0x7C, 0x00, 0x38, 0x38, 0x00, 0x38, 0x7C, 0xFE, 0x00
};

PROGMEM static const uint8_t imgSoundOffOn[][6] = { // 6x8 x2
    { 0x00, 0x00, 0xA0, 0x40, 0xA0, 0x00 },
    { 0x28, 0x10, 0x44, 0x38, 0x82, 0x7C },
};

struct MenuState {
    int8_t menuX;
    int8_t menuY;
    int8_t menuW;
    int8_t menuH;
    bool isFramed;
    bool isControlSound;
    int8_t menuItemCount;
    ITEM_T menuItemAry[MENU_COUNT_MAX];
    int8_t menuItemPos;
    bool isInvalidMenu;
};
static MenuState *g_menu = nullptr;
#define menuX (g_menu->menuX)
#define menuY (g_menu->menuY)
#define menuW (g_menu->menuW)
#define menuH (g_menu->menuH)
#define isFramed (g_menu->isFramed)
#define isControlSound (g_menu->isControlSound)
#define menuItemCount (g_menu->menuItemCount)
#define menuItemAry (g_menu->menuItemAry)
#define menuItemPos (g_menu->menuItemPos)
#define isInvalidMenu (g_menu->isInvalidMenu)

/*---------------------------------------------------------------------------*/

void clearMenuItems(void)
{
    menuItemCount = 0;
    dprintln(F("Clear menu items"));
}

void addMenuItem(const __FlashStringHelper *label, void (*func)(void))
{
    if (menuItemCount >= MENU_COUNT_MAX) return;
    ITEM_T *pItem = &menuItemAry[menuItemCount];
    pItem->label = label;
    pItem->func = func;
    menuItemCount++;
    if (label != NULL) {
        dprint(F("Add menu items: "));
        dprintln(label);
    }
}

int8_t getMenuItemPos(void)
{
    return menuItemPos;
}

int8_t getMenuItemCount(void)
{
    return menuItemCount;
}

void setMenuCoords(int8_t x, int8_t y, int8_t w, int8_t h, bool f, bool s)
{
    menuX = x;
    menuY = y;
    menuW = w;
    menuH = h;
    isFramed = f;
    isControlSound = s;
}

void setMenuItemPos(int8_t pos)
{
    menuItemPos = pos;
    isInvalidMenu = true;
    dprint(F("menuItemPos="));
    dprintln(menuItemPos);
}

void setConfirmMenu(int8_t y, void (*funcOk)(), void (*funcCancel)())
{
    playSoundClick();
    clearMenuItems();
    addMenuItem(F("OK"), funcOk);
    addMenuItem(F("CANCEL"), funcCancel);
    setMenuCoords(40, y, 47, 11, true, true);
    setMenuItemPos(1);
    isInvalidMenu = true;
}

void handleMenu(void)
{
    if (arduboy.buttonDown(UP_BUTTON) && menuItemPos > 0) {
        while (!(menuItemAry[--menuItemPos].func)) { ; }
        playSoundTick();
        isInvalidMenu = true;
        dprint(F("menuItemPos="));
        dprintln(menuItemPos);
    }
    if (arduboy.buttonDown(DOWN_BUTTON) && menuItemPos < menuItemCount - 1) {
        while (!(menuItemAry[++menuItemPos].func)) { ; }
        playSoundTick();
        isInvalidMenu = true;
        dprint(F("menuItemPos="));
        dprintln(menuItemPos);
    }
    if (isControlSound && arduboy.buttonDown(A_BUTTON)) {
        setSound(!arduboy.isAudioEnabled());
        playSoundClick();
        isInvalidMenu = true;
    }
    if (arduboy.buttonDown(B_BUTTON)) {
        menuItemAry[menuItemPos].func();
    }
}

void drawMenuItems(bool isForced)
{
    if (!isInvalidMenu && !isForced) return;
    arduboy.fillRect(menuX - 1, menuY - 1, menuW + 2, menuH + 2, BLACK);
    if (isFramed) {
        arduboy.drawRect(menuX - 2, menuY - 2, menuW + 4, menuH + 4, WHITE);
    }
    ITEM_T *pItem = menuItemAry;
    int8_t y = menuY;
    for (int i = 0; i < menuItemCount; i++, pItem++) {
        if (pItem->label != NULL) {
            arduboy.printEx(menuX + 12 - (i == menuItemPos) * 4, y, pItem->label);
            if (i == menuItemPos) arduboy.fillRect(menuX, y, 5, 5, WHITE);
            y += 6;
        } else {
            y += 2;
        }
    }
    if (isControlSound) drawSoundEnabled();
    isInvalidMenu = false;
}

void drawSoundEnabled(void)
{
    uint8_t *p = arduboy.getBuffer();
    memcpy(p + WIDTH * 8 - IMG_SOUND_W - IMG_SOUNDM_W, imgSound, IMG_SOUND_W);
    memcpy(p + WIDTH * 8 - IMG_SOUNDM_W, imgSoundOffOn[arduboy.isAudioEnabled()], IMG_SOUNDM_W);
}

#undef menuX
#undef menuY
#undef menuW
#undef menuH
#undef isFramed
#undef isControlSound
#undef menuItemCount
#undef menuItemAry
#undef menuItemPos
#undef isInvalidMenu
