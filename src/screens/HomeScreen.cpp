#include "Screens.h"
void screens::home(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t now) {
    // Deliberate simulated clock, never presented as NTP-synchronised time.
    uint32_t minutes = (9 * 60 + 42 + now / 60000) % 1440;
    char text[16]; snprintf(text, sizeof(text), "%02lu:%02lu", (unsigned long)(minutes/60), (unsigned long)(minutes%60));
    header(c, text, ui, m);
    bool online = fresh(m.agents[0].online, m.agents[0].lastUpdate, now);
    c.text(2, 24, "CODEX");
    int radius = ui.animation.motion == Motion::Full && m.agents[0].working && (now/500)%4 == 1 ? 3 : 2;
    c.dot(121, 20, online, radius);
    if (!online) { c.text(2, 42, "OFFLINE"); c.text(2, 57, "Waiting for data"); return; }
    c.text(2, 36, "5H"); c.percent(36, ui.animation[ShortNumber].value);
    c.bar(39, ui.animation[ShortBar].value);
    c.text(2, 54, "WK"); c.percent(54, ui.animation[WeekNumber].value);
    c.bar(57, ui.animation[WeekBar].value);
}
