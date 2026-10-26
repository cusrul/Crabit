#include "hal/font_engine.h"
#include <NanoUI/product/core/utf8.h>
#include "CrabitBuildProfile.h"
#if CRABIT_BW16_ENABLE_NETWORK_SYNC
#include "network_font_alias.h"
#else
#include "nanoui_assets.h"
#endif
#include <stdio.h>

uint32_t decode_utf8(const char** str) {
    return NanoUI::detail::decodeUtf8(str);
}

const uint8_t* get_chinese_glyph(uint32_t unicode, uint8_t target_size, uint8_t* out_width,
                                 uint8_t font_id) {
    if (cn_glyph_count == 0)
        return nullptr;

    // --- 第一轮：严格匹配 (Unicode + 字号 + 字体ID) ---
    for (int i = 0; i < cn_glyph_count; i++) {
        if (cn_glyph_index[i].unicode == unicode && cn_glyph_index[i].size == target_size &&
            cn_glyph_index[i].font_id == font_id) {
            if (out_width)
                *out_width = cn_glyph_index[i].width;
            return &cn_bitmap_pool[cn_glyph_index[i].offset];
        }
    }

    // --- 第二轮：模糊匹配 (只要 Unicode 和字号对上，不管 ID 是多少) ---
    // 字形查找缺失时会渲染为方块，因此需要如下校验：
    // 当 Python 把 STYLE_ART 标记为 ID:1，而 C++ 默认传 ID:0 时，依然能找到字
    for (int i = 0; i < cn_glyph_count; i++) {
        if (cn_glyph_index[i].unicode == unicode && cn_glyph_index[i].size == target_size) {
            if (out_width)
                *out_width = cn_glyph_index[i].width;
            return &cn_bitmap_pool[cn_glyph_index[i].offset];
        }
    }

    return nullptr;
}

int16_t get_utf8_string_width(const char* text, uint8_t size, uint8_t font_id) {
    if (!text)
        return 0;
    int16_t total_w = 0;

    const char* p = text;
    while (*p) {
        uint32_t u = decode_utf8(&p);

        uint8_t glyph_w;
        if (get_chinese_glyph(u, size, &glyph_w, font_id)) {
            total_w += glyph_w;
        } else if (u < 128) {
            uint8_t mul = (size >= 16) ? (size / 8) : 1;
            total_w += 6 * mul;
        } else {
            total_w += size;
        }
    }
    return total_w;
}
