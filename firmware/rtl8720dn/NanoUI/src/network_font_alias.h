#ifndef CRABIT_NETWORK_FONT_ALIAS_H
#define CRABIT_NETWORK_FONT_ALIAS_H
// Keep the normal App's generated resources intact. The linker discards the
// unused pool when the network-trial port selects these independent symbols.
#define cn_bitmap_pool network_cn_bitmap_pool
#define cn_glyph_index network_cn_glyph_index
#define cn_glyph_count network_cn_glyph_count
#include "network_font_data.h"
#endif
