#include <Arduino.h>
#include "hardware/Display.h"
#include "hardware/Input.h"
#include "network/NetworkService.h"
#include "network/Provisioning.h"
#include "config/RuntimeConfig.h"
#include "ui/Renderer.h"

Display oled;
Input input;
Model model;
NetworkService network;
ScreenManager ui;
Renderer renderer(oled.canvas.getU8g2());
bool setupMode = false;

void setup() {
    Serial.begin(115200);
    Serial.println("[boot] Agent Deck setup started");
    Serial.flush();
    input.begin();
    Serial.println("[boot] input initialized");
    Serial.flush();
    oled.begin(&Serial);
    Serial.println("Agent Deck booted. Send d for OLED/I2C diagnostics; s for render diagnostics.");
    oled.reportI2c(Serial);
    strcpy(model.agents[0].name, "Codex");
    strcpy(model.agents[1].name, "Claude");
    strcpy(model.agents[2].name, "OpenCode");
    ui.begin(model, millis());
    auto networkConfig = config::networkConfig();
    provisioning::load(networkConfig);
    setupMode = !provisioning::saved();
    setupMode |= digitalRead(config::Back) == LOW;
    if (setupMode) {
        provisioning::begin(networkConfig.deviceId);
        ui.notify("BLUETOOTH SETUP", millis());
    } else if (!network.begin(networkConfig)) ui.notify("NETWORK START FAILED", millis());
}
void loop() {
    uint32_t now = millis();
    if (setupMode) {
        provisioning::poll();
        static uint32_t setupNotice = 0;
        if (uint32_t(now - setupNotice) >= 1400) { ui.notify("BLUETOOTH SETUP", now); setupNotice = now; }
    }
    // Bound per-loop work; ISR continues to collect edges during I2C transfer.
    for (int i=0; i<16; ++i) {
        auto event = input.poll(now);
        if (event == InputEvent::NONE) break;
        ui.input(event, model, now);
        if (ui.commandRequested != ScreenManager::Command::None) {
            const char* action = ui.commandRequested == ScreenManager::Command::Confirm ? "confirm" :
                ui.commandRequested == ScreenManager::Command::Cancel ? "cancel" : "stop";
            if (!model.network.mqtt || !fresh(model.agents[ui.agent].online, model.agents[ui.agent].lastUpdate, now))
                ui.notify("AGENT OFFLINE", now);
            else if (!network.command(ui.agent, action, now)) ui.notify("COMMAND QUEUE FULL", now);
            ui.commandRequested = ScreenManager::Command::None;
        }
    }
    NetworkService::Snapshot snapshot;
    if (network.receive(snapshot)) {
        memcpy(model.agents, snapshot.agents, sizeof(model.agents));
        model.pc = snapshot.pc; model.network = snapshot.network;
        model.device.otaReady = snapshot.otaReady; model.device.timeSynced = snapshot.timeSynced;
        model.device.clockMinutes = snapshot.minutes; ++model.revision;
    }
    NetworkService::Event event;
    for (int i = 0; i < 8 && network.event(event); ++i) {
        if (event.kind == NetworkService::Event::Notice) ui.notify(event.text, now);
        else {
            if (event.contrast >= 0) model.device.contrast = event.contrast;
            if (event.motion >= 0) ui.animation.setMotion(static_cast<Motion>(event.motion));
            ++model.revision;
        }
    }
    static uint8_t contrast = 160;
    if (contrast != model.device.contrast) { contrast = model.device.contrast; oled.contrast(contrast); }
    if (ui.update(model, now)) renderer.invalidate();
    uint32_t start = micros();
    if (renderer.render(ui, model, now)) {
        auto& d = model.device;
        d.lastRenderUs = micros() - start;
        if (d.lastRenderUs > d.maxRenderUs) d.maxRenderUs = d.lastRenderUs;
        if (d.lastRenderUs > config::FrameMs * 1000) ++d.overBudget;
        ++d.frames;
    }
    // Diagnostics are explicitly requested, never emitted in the input ISR.
    if (Serial.available()) {
        switch (Serial.read()) {
            case 'q':
                Serial.printf("STATE wifi=%d mqtt=%d online=%d working=%d usage=%d five=%u week=%u age=%lu page=%d idle=%d sleep=%d\n",
                    model.network.wifi, model.network.mqtt,
                    fresh(model.agents[0].online, model.agents[0].lastUpdate, now), model.agents[0].working,
                    model.agents[0].usageKnown, model.agents[0].shortUsage, model.agents[0].weeklyUsage,
                    (unsigned long)(now-model.agents[0].lastUpdate), int(ui.page), ui.buddy.idle, ui.buddy.sleeping);
                break;
            case 'f': {
                Serial.print("FRAME ");
                const auto* buffer = oled.canvas.getBufferPtr();
                for (int i=0; i<1024; ++i) Serial.printf("%02x", buffer[i]);
                Serial.println();
                break;
            }
            case 'd': oled.reportI2c(Serial); break;
            case 's':
                Serial.printf("frames=%lu render_us=%lu max_us=%lu over_budget=%lu input_overflow=%lu heap=%u min_heap=%u\n",
                              (unsigned long)model.device.frames, (unsigned long)model.device.lastRenderUs,
                              (unsigned long)model.device.maxRenderUs, (unsigned long)model.device.overBudget,
                              (unsigned long)input.overflowCount(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
                break;
        }
    }
    yield();
}
