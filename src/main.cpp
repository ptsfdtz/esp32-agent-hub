#include <Arduino.h>
#include "hardware/Display.h"
#include "hardware/Input.h"
#include "services/MockService.h"
#include "ui/Renderer.h"

Display oled;
Input input;
Model model;
MockService mock;
ScreenManager ui;
Renderer renderer(oled.canvas.getU8g2());

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
    mock.begin(model, millis());
    ui.begin(model, millis());
}
void loop() {
    uint32_t now = millis();
    // Bound per-loop work; ISR continues to collect edges during I2C transfer.
    for (int i=0; i<16; ++i) {
        auto event = input.poll(now);
        if (event == InputEvent::NONE) break;
        ui.input(event, model, now);
    }
    mock.update(model, now);
    if (ui.sampleRequested) { mock.changeSample(model, now); ui.sampleRequested = false; }
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
