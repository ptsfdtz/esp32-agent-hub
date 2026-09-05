#include "Screens.h"
void screens::pc(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t now) {
    header(c, "PC", ui, m);
    if (!fresh(m.pc.online, m.pc.lastUpdate, now)) { c.text(2, 36, "OFFLINE"); return; }
    c.text(2, 24, "CPU"); c.percent(24, ui.animation[Cpu].value);
    c.text(2, 37, "RAM"); c.percent(37, ui.animation[Ram].value);
    c.text(2, 50, "GPU"); c.percent(50, ui.animation[Gpu].value);
    c.text(2, 63, "NET DOWN");
    char text[16]; snprintf(text, sizeof(text), "%luM", (unsigned long)(m.pc.downKbps/1000)); c.right(63, text);
}
