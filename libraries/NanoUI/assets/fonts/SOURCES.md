# Default input fonts

These five BDF inputs are copied byte-for-byte from the existing STM32 NanoUI
assets/fonts/bdf directory. They are font source inputs, not application-generated
font subsets. Existing product input directories are not moved/deleted.

The original BDF copyright/license comments are retained. WenQuanYi BDF headers
identify their authors and GPL v2 with a font embedding exception; inspect each
font's own terms before redistribution. This document does not relicense fonts.
No external font download or Windows system-font dependency is introduced.

Default mappings: 8 -> 5x8.bdf; 10 -> 6x10.bdf; 12 -> wqy12.bdf;
16 -> wqy16.bdf; 24 -> gb24st.bdf. Other fonts/sizes and STYLE_ART require an
explicit fonts_dir/ttf_font when no matching default glyph exists.
