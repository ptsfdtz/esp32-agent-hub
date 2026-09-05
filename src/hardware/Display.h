#pragma once
#include <U8g2lib.h>
#include "config/Config.h"

class Display {
public:
    U8G2_SH1106_128X64_NONAME_F_HW_I2C canvas{U8G2_R0, U8X8_PIN_NONE,
                                                            config::Scl, config::Sda};
    void begin(Stream* diagnostics = nullptr);
    void contrast(uint8_t value) { canvas.setContrast(value); }
    // Probe only the two normal 7-bit addresses for this class of OLED.
    // This is intentionally not a broad bus scan in the normal UI loop.
    void reportI2c(Stream& output);
    bool detected() const { return detectedAddress_ != 0; }
    uint8_t detectedAddress() const { return detectedAddress_; }
private:
    uint8_t probe(uint8_t address);
    uint8_t detectedAddress_ = 0;
    uint8_t response3c_ = 0xFF;
    uint8_t response3d_ = 0xFF;
};
