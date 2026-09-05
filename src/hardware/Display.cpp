#include "Display.h"
#include <Wire.h>
#include "ui/Typography.h"

uint8_t Display::probe(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission();
}
void Display::begin(Stream* diagnostics) {
    bool wireStarted = Wire.begin(config::Sda, config::Scl);
    if (diagnostics) {
        diagnostics->printf("[display] Wire.begin SDA=%u SCL=%u: %s\n",
                            config::Sda, config::Scl, wireStarted ? "OK" : "FAILED");
        diagnostics->flush();
    }
    Wire.setClock(config::I2cHz);
    // A full OLED refresh is split into I2C writes, but a 10ms bus timeout is
    // unnecessarily tight on a real desk cable. This is a failure bound, not
    // an animation delay.
    Wire.setTimeOut(50);
    response3c_ = probe(0x3C);
    response3d_ = probe(0x3D);
    detectedAddress_ = response3c_ == 0 ? 0x3C : response3d_ == 0 ? 0x3D : 0;
    if (diagnostics) {
        diagnostics->printf("[display] probe 0x3C=%u 0x3D=%u selected=%s\n",
                            response3c_, response3d_, detectedAddress_ ? "YES" : "NONE");
        diagnostics->flush();
    }
    if (detectedAddress_) canvas.setI2CAddress(detectedAddress_ << 1);
    // U8g2 owns initialization of its hardware-I2C transport. Arduino-ESP32
    // 2.0.17 can block when U8g2 calls Wire.begin() on the bus we opened for
    // the preceding probe. Release it first; U8g2 immediately reopens the same
    // configured SDA/SCL pins from its constructor.
    Wire.end();
    canvas.setBusClock(config::I2cHz);
    if (diagnostics) { diagnostics->println("[display] U8g2 begin..."); diagnostics->flush(); }
    canvas.begin();
    if (diagnostics) { diagnostics->println("[display] U8g2 begin complete"); diagnostics->flush(); }
    Wire.setClock(config::I2cHz);
    canvas.setFont(typography::body());
    canvas.setFontMode(1);
    canvas.setContrast(160);
}
void Display::reportI2c(Stream& output) {
    // Refresh values in case a display was connected after boot.
    response3c_ = probe(0x3C);
    response3d_ = probe(0x3D);
    detectedAddress_ = response3c_ == 0 ? 0x3C : response3d_ == 0 ? 0x3D : 0;
    output.printf("I2C SDA=%u SCL=%u clock=%luHz 0x3C=%s(code=%u) 0x3D=%s(code=%u)\n",
                  config::Sda, config::Scl, (unsigned long)config::I2cHz,
                  response3c_ == 0 ? "ACK" : "NO", response3c_,
                  response3d_ == 0 ? "ACK" : "NO", response3d_);
    if (detectedAddress_) {
        output.printf("OLED candidate found at 0x%02X; SH1106 renderer initialized.\n", detectedAddress_);
    } else {
        output.println("NO OLED ACK: check 3.3V/GND, GPIO8 SDA, GPIO9 SCL, and I2C-vs-SPI module type.");
    }
}
