#include "Screens.h"
#include "BuddyDrawing.h"
void screens::home(Canvas& c,const ScreenManager& ui,const Model& m,uint32_t now) {
    uint32_t minutes=(9*60+42+now/60000)%1440;
    char text[20];snprintf(text,sizeof(text),"%02lu:%02lu",(unsigned long)(minutes/60),(unsigned long)(minutes%60));
    header(c,text,ui,m);
    bool online=fresh(m.agents[0].online,m.agents[0].lastUpdate,now);
    buddyDrawing::eyes(c,ui,27,28,13);
    c.title();c.text(60,26,"Codex");c.font();
    c.small();c.text(60,38,!online?"Offline":m.agents[0].working?"Working":"Ready");
    if(!online){c.center(57,"Waiting for data");c.font();return;}
    snprintf(text,sizeof(text),"5h  %d%%",int(lroundf(ui.animation[ShortNumber].value)));c.text(4,52,text);
    snprintf(text,sizeof(text),"Week %d%%",int(lroundf(ui.animation[WeekNumber].value)));c.text(69,52,text);
    c.meter(4,57,54,ui.animation[ShortBar].value);c.meter(69,57,54,ui.animation[WeekBar].value);c.font();
}
