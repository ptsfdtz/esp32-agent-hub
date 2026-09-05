#include "MockService.h"
#include <stdio.h>
void MockService::begin(Model& m, uint32_t now) {
    snprintf(m.agents[0].name, sizeof(m.agents[0].name), "Codex");
    snprintf(m.agents[1].name, sizeof(m.agents[1].name), "Claude");
    snprintf(m.agents[2].name, sizeof(m.agents[2].name), "OpenCode");
    auto& a = m.agents[0];
    a.online = a.working = true;
    a.shortUsage = 72; a.weeklyUsage = 41;
    a.shortReset = 12360; a.weeklyReset = 240000;
    snprintf(a.model, sizeof(a.model), "MOCK MODEL");
    snprintf(a.task, sizeof(a.task), "Implement OLED UI");
    a.lastUpdate = now;
    m.pc.online = true; m.pc.cpu = 32; m.pc.ram = 61; m.pc.gpu = 47;
    m.pc.downKbps = 12000; m.pc.lastUpdate = now;
    m.network.wifi = m.network.mqtt = true;
    snprintf(m.network.ssid, sizeof(m.network.ssid), "AgentDeck Mock");
    snprintf(m.network.ip, sizeof(m.network.ip), "192.0.2.10");
    snprintf(m.network.mqttHost, sizeof(m.network.mqttHost), "mock.local");
    m.network.rssi = -48; m.network.latencyMs = 12;
    heartbeat_ = now; ++m.revision;
}
void MockService::update(Model& m, uint32_t now) {
    if (uint32_t(now - heartbeat_) < 1000) return;
    heartbeat_ = now;
    // Heartbeats only; sample values remain stable until explicitly changed.
    m.agents[0].lastUpdate = now; m.pc.lastUpdate = now;
}
void MockService::changeSample(Model& m, uint32_t now) {
    alternate_ = !alternate_;
    m.agents[0].shortUsage = alternate_ ? 80 : 72;
    m.agents[0].weeklyUsage = alternate_ ? 45 : 41;
    m.pc.cpu = alternate_ ? 58 : 32;
    m.pc.ram = alternate_ ? 67 : 61;
    m.pc.gpu = alternate_ ? 71 : 47;
    m.agents[0].lastUpdate = now; ++m.revision;
}
