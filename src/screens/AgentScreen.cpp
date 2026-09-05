#include "Screens.h"
#include "BuddyDrawing.h"
void screens::agents(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t now) {
    if (ui.page == Page::Agents) {
        header(c, "Agents", ui, m);
        const char* labels[] = {m.agents[0].name, m.agents[1].name, m.agents[2].name};
        menu(c, ui, labels, 3);
        // Status stays outside the selection box. Circles cannot use XOR:
        // U8g2's symmetric rasterizer can visit the same pixel more than once.
        c.color(1);
        for (int i=0; i<3; ++i) c.dot(119, 22+i*14, fresh(m.agents[i].online, m.agents[i].lastUpdate, now));
        c.color(1); return;
    }
    const auto& a = m.agents[ui.agent];
    header(c, a.name, ui, m);
    bool online = fresh(a.online, a.lastUpdate, now);
    c.text(4, 25, online ? a.working ? "Working" : "Ready" : "Offline");
    buddyDrawing::eyes(c,ui,107,24,8);
    if (!online) {
        if (!a.lastUpdate) c.text(2, 43, "No data received");
        else {
            c.text(2, 43, "Last update");
            char text[22]; uint32_t sec = (now - a.lastUpdate) / 1000;
            snprintf(text, sizeof(text), "%02lu:%02lu ago", (unsigned long)(sec/60), (unsigned long)(sec%60));
            c.text(2, 57, text);
        }
        return;
    }
    c.clippedText(2, 38, a.task);
    c.clippedText(2, 50, a.model);
    char text[22];
    if (!a.usageKnown) { c.text(2, 63, "5H --  --:--"); return; }
    uint32_t elapsed = uint32_t(now - a.usageUpdated) / 1000;
    uint32_t reset = a.shortReset > elapsed ? a.shortReset - elapsed : 0;
    snprintf(text, sizeof(text), "5H %d%%  %02lu:%02lu", int(lroundf(ui.animation[ShortNumber].value)),
             (unsigned long)(reset/3600), (unsigned long)(reset/60%60));
    c.text(2, 63, text);
}
