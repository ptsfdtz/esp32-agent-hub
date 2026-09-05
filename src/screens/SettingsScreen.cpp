#include "Screens.h"
void screens::settings(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t) {
    const char* labels[] = {"WiFi", "MQTT", "Brightness", "Animation", "Firmware", "About"};
    if (ui.page == Page::Settings) { header(c, "Settings", ui, m); menu(c, ui, labels, 6); return; }
    header(c, labels[ui.setting], ui, m);
    char text[22];
    switch (ui.setting) {
        case 0: c.text(4, 29, m.network.wifi ? "Connected" : "Disconnected"); c.clippedText(4, 47, m.network.ssid); break;
        case 1: c.text(4, 29, m.network.mqtt ? "Connected" : "Disconnected"); c.clippedText(4, 47, m.network.mqttHost); break;
        case 2:
            snprintf(text, sizeof(text), "%u / 255", m.device.contrast);
            c.center(32, text); c.meter(8,47,112,m.device.contrast*100.0f/255); break;
        case 3:
            c.center(35, ui.animation.motion == Motion::Full ? "Full motion" : ui.animation.motion == Motion::Reduced ? "Reduced motion" : "Motion off");
            break;
        case 4:
            c.text(2, 25, config::Firmware);
            c.text(4, 39, m.device.otaReady ? "OTA ready" : "OTA not enabled");
            snprintf(text, sizeof(text), "FRAME %lu.%lums", (unsigned long)(m.device.lastRenderUs/1000), (unsigned long)(m.device.lastRenderUs/100%10));
            c.text(2, 53, text); break;
        case 5:
            c.title();c.center(29,"Agent Deck");c.font();c.center(46,"Your little desk buddy");c.small();c.center(61,config::Firmware);c.font();break;
    }
}
