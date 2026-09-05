#include "Screens.h"
#include "BuddyDrawing.h"
void screens::header(Canvas& c, const char* title, const ScreenManager& ui, const Model& m) {
    c.text(2 + int(lroundf(ui.animation[Title].value)), 10, title);
    if (m.device.mock) c.right(10, "M");
    buddyDrawing::header(c, ui);
}
void screens::menu(Canvas& c, const ScreenManager& ui, const char* const* labels, int count) {
    const int scroll = int(lroundf(ui.animation[Scroll].value));
    const int selection = int(lroundf(ui.animation[Selection].value));
    c.clip(16, 60);
    c.box(0, 16 + selection - scroll, ui.page == Page::Agents ? 110 : 128, 14);
    c.color(2); // XOR keeps letters legible throughout highlight movement.
    for (int i = 0; i < count; ++i) {
        int baseline = 26 + i * 14 - scroll;
        if (baseline > 6 && baseline < 70) c.text(5, baseline, labels[i]);
    }
    c.color(1); c.unclip();
    if (count > 3) {
        c.box(123, 62, 3, 1);
        c.box(2 + ui.selected * 18, 62, 12, 1);
    }
}
void screens::launcher(Canvas& c, const ScreenManager& ui, const Model& m, uint32_t) {
    header(c, "AGENT DECK", ui, m);
    const char* labels[] = {"Home", "Agent", "PC", "IoT", "Timer", "Settings"};
    menu(c, ui, labels, 6);
}
