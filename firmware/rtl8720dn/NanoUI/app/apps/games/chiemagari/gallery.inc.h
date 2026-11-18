// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
// `galleryToDrawAll` is renamed because the merged namespace also holds puzzle state.
#include "common.h"

/*  Defines  */

#define GALLERY_OFFSET_UNIT_X   -4
#define GALLERY_OFFSET_PIXEL_X  (GALLERY_OFFSET_UNIT_X * 7)

/*  Local Functions  */

static void setupGalleryPieces(void);
static void drawGalleryIndex(void);
static void drawPlayTime(void);
static void draw2Digits(uint8_t val, char c);

/*  Local Variables  */

PROGMEM static const uint8_t imgGallery[108] = { // "Gallery" 54x16
    0xF0, 0xFC, 0x06, 0x02, 0x01, 0x01, 0x01, 0x21, 0x22, 0xE6, 0xEF, 0x20, 0x00, 0x60, 0x70, 0x10,
    0x90, 0xF0, 0xE0, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0x00, 0x00, 0xC0,
    0xE0, 0x50, 0x50, 0x70, 0x60, 0x00, 0x20, 0xF0, 0xF0, 0x20, 0x10, 0x30, 0x00, 0x10, 0x70, 0xF0,
    0x90, 0x00, 0x00, 0x90, 0x70, 0x10, 0x01, 0x03, 0x07, 0x04, 0x0C, 0x08, 0x08, 0x08, 0x08, 0x0F,
    0x07, 0x00, 0x00, 0x0E, 0x0F, 0x09, 0x08, 0x0F, 0x0F, 0x08, 0x00, 0x08, 0x0F, 0x0F, 0x08, 0x00,
    0x08, 0x0F, 0x0F, 0x08, 0x00, 0x03, 0x07, 0x0E, 0x0C, 0x0C, 0x06, 0x00, 0x08, 0x0F, 0x0F, 0x08,
    0x00, 0x00, 0x00, 0x00, 0xC0, 0xC1, 0x67, 0x1E, 0x06, 0x01, 0x00, 0x00,
};

struct GalleryState {
    bool galleryToDrawAll;
    uint8_t galleryIdx;
};
static GalleryState *g_gallery = nullptr;
#define galleryToDrawAll (g_gallery->galleryToDrawAll)
#define galleryIdx (g_gallery->galleryIdx)

/*---------------------------------------------------------------------------*/
/*                              Main Functions                               */
/*---------------------------------------------------------------------------*/

void initGallery(void)
{
    setupGalleryPieces();
    padRepeatCount = 0;
    helpY = HEIGHT;
    galleryToDrawAll = true;
}

MODE_T updateGallery(void)
{
    MODE_T ret = MODE_GALLERY;
    handleDPad();
    if (arduboy.buttonDown(A_BUTTON)) {
        playSoundClick();
        helpY = HEIGHT + 1;
        galleryToDrawAll = true;
        ret = MODE_MENU;
    } else if (padX != 0 && galleryIdx + padX >= 0 && galleryIdx + padX < clearCount) {
        playSoundTick();
        galleryIdx += padX;
        setupGalleryPieces();
        galleryToDrawAll = true;
    }
    if (helpY > HELP_TOP_LIMIT) {
        helpY--;
    }
    return ret;
}

void drawGallery(void)
{
    if (galleryToDrawAll) {
        arduboy.clear();
        drawBoard(GALLERY_OFFSET_PIXEL_X);
        for (int i = 0; i < PIECES; i++) {
            drawPiece(i);
        }
        drawGalleryIndex();
        drawPlayTime();
        galleryToDrawAll = false;
    }
    if (isHelpVisible) {
        drawHelp(HELP_GALLERY, HELP_RIGHT_POS, helpY);
    }
}

/*---------------------------------------------------------------------------*/
/*                             Control Functions                             */
/*---------------------------------------------------------------------------*/

void setGalleryIndex(uint8_t idx)
{
    galleryIdx = idx;
}

static void setupGalleryPieces(void)
{
    CODE_T code[PIECES];
    readEncodedPieces(galleryIdx, code);
    decodePieces(code);
    for (int i = 0; i < PIECES; i++) {
        pieceAry[i].x += GALLERY_OFFSET_UNIT_X;
    }
}

/*---------------------------------------------------------------------------*/
/*                              Draw Functions                               */
/*---------------------------------------------------------------------------*/

static void drawGalleryIndex(void)
{
    arduboy.drawBitmap(72, 4, imgGallery, 54, 16, WHITE);
    arduboy.setCursor(96, 22);
    draw2Digits(galleryIdx + 1, ' ');
    arduboy.print('/');
    draw2Digits(clearCount, ' ');
}

static void drawPlayTime(void)
{
    arduboy.printEx(72, 34, F("PLAY TIME"));
    arduboy.setCursor(72, 40);
    char buf[16];
    snprintf(buf, sizeof(buf), "%6lu:", (unsigned long) (playFrames / 216000UL));
    arduboy.print(buf);
    draw2Digits(playFrames / 3600 % 60, '0');
}

static void draw2Digits(uint8_t val, char c)
{
    if (val < 10) {
        arduboy.print(c);
    }
    arduboy.print(val);
}
#undef galleryToDrawAll
#undef galleryIdx
