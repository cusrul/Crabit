#ifndef NANOUI_ASSETS_H
#define NANOUI_ASSETS_H

#include <stdint.h>

extern const uint8_t asset_my_media[];

// === FONT ENGINE STRUCTURES ===
struct CN_GlyphIndex {
    uint32_t unicode;
    uint8_t size;
    uint8_t width;
    uint8_t font_id;
    uint32_t offset;
};
extern const uint8_t cn_bitmap_pool[];
extern const CN_GlyphIndex cn_glyph_index[];
extern const uint16_t cn_glyph_count;

#endif
