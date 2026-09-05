#pragma once
#include <clib/u8g2.h>
// Native monochrome glyphs: avoid downsampling desktop antialiased fonts.
namespace typography {
inline const uint8_t* body() { return u8g2_font_profont11_mr; }
inline const uint8_t* number() { return u8g2_font_profont22_tn; }
}
