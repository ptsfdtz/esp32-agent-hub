#include "Screens.h"
void screens::settings(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t) {
    const char* labels[] = {"WiFi", "MQTT", "Brightness", "Animation", "Firmware", "About"};
    if (ui.page == Page::Settings) { header(c, "SETTINGS", ui, m); menu(c, ui, labels, 6); return; }
    header(c, labels[ui.setting], ui, m);
    char text[22];
    switch (ui.setting) {
        case 0: c.text(2, 28, "MOCK / NO RADIO"); c.clippedText(2, 46, m.network.ssid); break;
        case 1: c.text(2, 28, "MOCK / NO BROKER"); c.clippedText(2, 46, m.network.mqttHost); break;
        case 2:
            snprintf(text, sizeof(text), "%u / 255", m.device.contrast);
            c.text(2, 32, text); c.bar(47, m.device.contrast*100.0f/255); break;
        case 3:
            c.text(2, 33, ui.animation.motion == Motion::Full ? "FULL" : ui.animation.motion == Motion::Reduced ? "REDUCED" : "OFF");
            break;
        case 4:
            c.text(2, 25, config::Firmware);
            c.text(2, 39, m.device.otaReady ? "OTA READY" : "OTA NOT ENABLED");
            snprintf(text, sizeof(text), "FRAME %lu.%lums", (unsigned long)(m.device.lastRenderUs/1000), (unsigned long)(m.device.lastRenderUs/100%10));
            c.text(2, 53, text); break;
        case 5:
            c.text(2, 27, "AGENT DECK"); c.text(2, 43, "ESP32-S3 / SH1106"); c.text(2, 60, "PHASE 1 - MOCK"); break;
    }
}
