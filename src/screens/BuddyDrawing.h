#pragma once
#include "Canvas.h"
#include "ui/ScreenManager.h"

namespace buddyDrawing {
// Eye dimensions interpolate continuously; no bitmap frame switching.
inline void eyes(Canvas& c,const ScreenManager& ui,int center,int cy,int size) {
    float lid=ui.animation[BuddyLid].value, smile=ui.animation[BuddySmile].value;
    float curious=ui.animation[BuddyCurious].value, wink=ui.animation[BuddyWink].value;
    float focus=ui.animation[BuddyFocus].value;
    int gaze=int(lroundf(ui.animation[BuddyLook].value*size/10));
    int lift=int(lroundf(ui.animation[BuddyLift].value));
    for(int i=0;i<2;++i) {
        float closed=lid+(1-lid)*(i?wink:0);
        int h=int(lroundf((size+curious*3)*(1-closed)*(1-.55f*focus)));
        h=h<2?2:h;
        int x=center+(i?size/4:-size-size/4)+gaze;
        int y=cy-h/2+lift;
        c.rounded(x,y,size,h,size/4);
        if(smile>.01f && h>3) {
            c.color(0);
            c.rounded(x-1,y+h-int(lroundf(smile*h*.60f)),size+2,h,size/3);
            c.color(1);
        }
    }
}
inline void standby(Canvas& c,const ScreenManager& ui,const Model& model,uint32_t now) {
    float reveal=ui.animation[IdleReveal].value;
    if(reveal<=0) return;
    int top=64-int(lroundf(48*reveal));
    c.clip(16,64);c.color(0);c.box(0,top,128,48);c.color(1);
    eyes(c,ui,64,top+19,24);
    char text[24];
    if(fresh(model.agents[0].online,model.agents[0].lastUpdate,now))
        snprintf(text,sizeof(text),"5h %u%%  /  week %u%%",model.agents[0].shortUsage,model.agents[0].weeklyUsage);
    else snprintf(text,sizeof(text),"Codex offline");
    c.small();c.center(top+46,text);c.font();c.unclip();
}
}
