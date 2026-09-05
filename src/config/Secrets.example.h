#pragma once
// Copy to Secrets.h (ignored by git).
namespace config {
inline NetworkConfig networkConfig() {
    NetworkConfig c;
    // snprintf(c.ssid, sizeof(c.ssid), "%s", "YOUR_WIFI");
    // snprintf(c.password, sizeof(c.password), "%s", "YOUR_PASSWORD");
    // snprintf(c.mqttHost, sizeof(c.mqttHost), "%s", "192.168.1.10");
    // snprintf(c.mqttUser, sizeof(c.mqttUser), "%s", "agentdeck");
    // snprintf(c.mqttPassword, sizeof(c.mqttPassword), "%s", "BROKER_PASSWORD");
    // snprintf(c.otaPassword, sizeof(c.otaPassword), "%s", "CHOOSE_OTA_PASSWORD");
    return c;
}
}
