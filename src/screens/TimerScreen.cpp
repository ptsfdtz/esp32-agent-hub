#include "Screens.h"
void screens::timer(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t) {
    header(c, "Focus", ui, m);
    uint32_t seconds = ui.timer.seconds();
    char text[12]; snprintf(text, sizeof(text), "%02lu:%02lu", (unsigned long)(seconds/60), (unsigned long)(seconds%60));
    c.font(true); c.center(43, text); c.font();
    c.center(59, ui.timer.complete ? "All done!" : ui.timer.running ? "Focusing" : "Ready when you are");
}
