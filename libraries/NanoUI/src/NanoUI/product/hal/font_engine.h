#ifndef NANOUI_FONT_ENGINE_H
#define NANOUI_FONT_ENGINE_H

#include <stdint.h>
#include <stddef.h>

/**
 * 样式标记宏：仅供 Python 脚本扫描，C++ 运行时无开销。
 */
#define STYLE_BDF(str) str  // 强制使用 BDF 点阵库 (ID: 0)
#define STYLE_ART(str) str  // 强制使用 TTF 艺术库 (ID: 1)

/**
 * Decode one scalar from a NUL-terminated string and advance the borrowed cursor.
 * Invalid input consumes one byte and returns U+FFFD. End/null returns zero
 * without advancing. Providers delegate to the platform-independent UTF-8 core.
 */
uint32_t decode_utf8(const char** str);

/**
 * 在字库索引中查找字符位图
 * @param target_size 期望的像素字号
 * @param font_id 期望的字体ID (0: BDF, 1: TTF)
 * @param out_width 如果找到，返回该字符的真实像素宽度
 */
const uint8_t* get_chinese_glyph(uint32_t unicode, uint8_t target_size, uint8_t* out_width,
                                 uint8_t font_id = 0);

/**
 * 计算 UTF-8 字符串在特定像素字号和字体ID下的总像素宽度
 */
int16_t get_utf8_string_width(const char* text, uint8_t size, uint8_t font_id = 0);

#endif
