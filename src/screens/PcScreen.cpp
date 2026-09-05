#include "Screens.h"
void screens::pc(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t now) {
    header(c, "Computer", ui, m);
    if (!fresh(m.pc.online, m.pc.lastUpdate, now)) { c.text(2, 36, "OFFLINE"); return; }
    c.text(2, 24, "CPU"); c.percent(24, ui.animation[Cpu].value);
    c.text(2, 37, "RAM"); c.percent(37, ui.animation[Ram].value);
    c.text(2, 50, "GPU"); c.percent(50, ui.animation[Gpu].value);
    c.meter(36,19,48,ui.animation[Cpu].value);
    c.meter(36,32,48,ui.animation[Ram].value);
    c.meter(36,45,48,ui.animation[Gpu].value);
    c.small();c.text(4, 63, "Download");
    char text[16]; snprintf(text, sizeof(text), "%luM", (unsigned long)(m.pc.downKbps/1000)); c.right(63, text);
    c.font();
}
