#pragma once
#include <stdint.h>
#include "config/Config.h"
struct AgentStatus {
    char name[16] = "";
    bool online = false, working = false;
    bool usageKnown = false;
    uint8_t shortUsage = 0, weeklyUsage = 0;
    uint32_t shortReset = 0, weeklyReset = 0, lastUpdate = 0, usageUpdated = 0;
    uint32_t completedAt = 0;
    char model[32] = "", task[80] = "";
};
struct PcStatus {
    bool online = false;
    bool gpuKnown = false;
    uint8_t cpu = 0, ram = 0, gpu = 0;
    uint32_t downKbps = 0, lastUpdate = 0;
};
struct NetworkStatus {
    bool wifi = false, mqtt = false, cloud = false;
    char ssid[33] = "", ip[16] = "", mqttHost[64] = "";
    int16_t rssi = 0;
    uint16_t latencyMs = 0;
};
struct DeviceStatus {
    bool otaReady = false;
    uint8_t contrast = 160;
    uint32_t frames = 0, lastRenderUs = 0, maxRenderUs = 0, overBudget = 0;
    bool timeSynced = false;
    uint16_t clockMinutes = 0;
};
struct Model {
    AgentStatus agents[3];
    PcStatus pc;
    NetworkStatus network;
    DeviceStatus device;
    uint32_t revision = 0;
};
inline bool fresh(bool online, uint32_t updated, uint32_t now) {
    return online && uint32_t(now - updated) < config::StaleMs;
}
