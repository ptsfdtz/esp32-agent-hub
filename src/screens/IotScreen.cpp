#include "Screens.h"
void screens::iot(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t now) {
    header(c, ui.page == Page::Iot ? "Network" : "Connection", ui, m);
    if (ui.page == Page::Iot) {
        const char* labels[] = {"WiFi", "MQTT", "PC", "Agent"};
        bool states[] = {m.network.wifi, m.network.mqtt, fresh(m.pc.online,m.pc.lastUpdate,now),
                        fresh(m.agents[0].online,m.agents[0].lastUpdate,now)};
        for (int i=0; i<4; ++i) {
            c.dot(7,21+i*13,states[i]);c.text(17,24+i*13,labels[i]);
            c.small();c.right(24+i*13,states[i]?"Online":"Offline");c.font();
        }
    } else {
        c.clippedText(2, 23, m.network.ssid);
        c.text(2, 36, m.network.ip);
        c.clippedText(2, 49, m.network.mqttHost);
        char text[22]; snprintf(text, sizeof(text), "%ddBm   %ums", m.network.rssi, m.network.latencyMs);
        c.text(2, 62, text);
    }
}
