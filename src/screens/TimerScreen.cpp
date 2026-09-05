#include "Screens.h"
void screens::timer(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t) {
    header(c, "FOCUS", ui, m);
    uint32_t seconds = ui.timer.seconds();
    char text[12]; snprintf(text, sizeof(text), "%02lu:%02lu", (unsigned long)(seconds/60), (unsigned long)(seconds%60));
    c.font(true); c.center(43, text); c.font();
    c.text(2, 61, ui.timer.complete ? "COMPLETE" : ui.timer.running ? "RUNNING" : "PAUSED");
}
