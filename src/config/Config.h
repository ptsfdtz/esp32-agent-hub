#pragma once
#include <stdint.h>

namespace config {
constexpr uint8_t Confirm = 15, Sda = 8, Scl = 9, Push = 6;
constexpr uint8_t EncoderA = 4, EncoderB = 5, Back = 7;
constexpr uint32_t I2cHz = 400000;
constexpr uint32_t FrameMs = 33, DebounceMs = 20, LongPressMs = 700;
// Change only this sign if the physical clockwise direction is reversed.
constexpr int EncoderDirection = 1;
constexpr uint8_t EncoderEdgesPerDetent = 4;
constexpr char Firmware[] = "0.1.0";
constexpr uint32_t StaleMs = 15000;
// Phase 2 configuration seam. No connections are made in Phase 1.
struct NetworkConfig {
    char ssid[33] = "";
    char password[65] = "";
    char mqttHost[64] = "";
    uint16_t mqttPort = 1883;
    char deviceId[33] = "agentdeck-01";
};
}
