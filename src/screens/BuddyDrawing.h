#pragma once
#include "Canvas.h"
#include "ui/ScreenManager.h"

namespace buddyDrawing {
inline void robot(Canvas& c, const ScreenManager& ui, int x, int y, int scale) {
    int lift = int(lroundf(ui.animation[BuddyLift].value));
    int look = int(lroundf(ui.animation[BuddyLook].value));
    y += lift;
    auto pixel = [&](int px,int py,int w,int h) { c.box(x+px*scale,y+py*scale,w*scale,h*scale); };
    // Tiny friendly terminal, with ears and little feet. No sprite allocation.
    pixel(1,0,2,2); pixel(13,0,2,2);
    pixel(2,2,12,1); pixel(0,4,1,6); pixel(15,4,1,6);
    pixel(1,3,1,1); pixel(14,3,1,1);
    pixel(1,10,14,1); pixel(3,11,3,1); pixel(10,11,3,1);
    bool shut = ui.animation[BuddyLid].value > .55f;
    auto eyes = [&](int ex, bool wink) {
        if (shut || wink) pixel(ex,6,3,1);
        else if (ui.buddy.expression == Expression::Happy) {
            pixel(ex,6,1,1); pixel(ex+1,5,1,1); pixel(ex+2,6,1,1);
        } else pixel(ex+1+look,5,1,ui.buddy.expression == Expression::Curious ? 3 : 2);
    };
    eyes(3,false); eyes(9,ui.buddy.expression == Expression::Wink);
    pixel(7,8,2,1);
    if (ui.buddy.expression == Expression::Hold) pixel(6,7,4,1);
}
inline void header(Canvas& c, const ScreenManager& ui) {
    robot(c, ui, 98, 2, 1);
}
inline void standby(Canvas& c, const ScreenManager& ui, const Model& model) {
    float reveal = ui.animation[IdleReveal].value;
    if (reveal <= 0) return;
    int top = 64 - int(lroundf(48 * reveal));
    c.clip(16,64); c.color(0); c.box(0,top,128,48); c.color(1);
    robot(c,ui,46,top+5,2);
    if (ui.buddy.sleeping) c.text(85,top+13,"zZ");
    char text[22];
    snprintf(text,sizeof(text),"5h %u%%   wk %u%%",model.agents[0].shortUsage,model.agents[0].weeklyUsage);
    c.center(top+46,text); c.unclip();
}
}
