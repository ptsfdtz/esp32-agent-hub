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
constexpr char Firmware[] = "0.3.0";
constexpr uint32_t StaleMs = 15000;
// Compile-time defaults; BLE provisioning overrides network fields from NVS.
struct NetworkConfig {
    char ssid[33] = "";
    char password[65] = "";
    char mqttHost[64] = "";
    uint16_t mqttPort = 1883;
    char deviceId[33] = "agentdeck-01";
    char mqttUser[65] = "", mqttPassword[65] = "";
    char otaPassword[65] = "";
    char timezone[64] = "CST-8";
    char ntpServer[64] = "pool.ntp.org";
};
}
