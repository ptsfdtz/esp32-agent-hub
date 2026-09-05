#pragma once
#include <clib/u8g2.h>
// Native monochrome glyphs: avoid downsampling desktop antialiased fonts.
namespace typography {
inline const uint8_t* body() { return u8g2_font_helvR08_tr; }
inline const uint8_t* title() { return u8g2_font_helvB08_tr; }
inline const uint8_t* small() { return u8g2_font_5x8_tr; }
inline const uint8_t* number() { return u8g2_font_profont22_tn; }
}
