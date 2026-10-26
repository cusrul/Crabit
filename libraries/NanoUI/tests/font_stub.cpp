// Standalone tests supply a resource provider without a firmware/SDK dependency.
#include <NanoUI/product/hal/font_engine.h>
#include <NanoUI/product/core/utf8.h>
#include <cstring>
uint32_t decode_utf8(const char** input) {
    return NanoUI::detail::decodeUtf8(input);
}
const uint8_t* get_chinese_glyph(uint32_t, uint8_t, uint8_t*, uint8_t) {
    return nullptr;
}
int16_t get_utf8_string_width(const char* text, uint8_t, uint8_t) {
    int16_t width = 0;
    while (*text) {
        decode_utf8(&text);
        width += 6;
    }
    return width;
}
