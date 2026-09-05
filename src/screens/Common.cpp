#include "Screens.h"
#include "Icons.h"
void screens::header(Canvas& c,const char* title,const ScreenManager& ui,const Model&) {
    c.title();c.text(4+int(lroundf(ui.animation[Title].value)),10,title);
    c.font();
}
void screens::menu(Canvas& c,const ScreenManager& ui,const char* const* labels,int count) {
    int scroll=int(lroundf(ui.animation[Scroll].value));
    int selection=int(lroundf(ui.animation[Selection].value));
    c.clip(15,60);
    c.rounded(2,16+selection-scroll,ui.page==Page::Agents?109:117,14,3,false);
    for(int i=0;i<count;++i){
        int baseline=26+i*14-scroll;
        if(baseline>6 && baseline<70) {
            c.color(0);c.box(8,baseline-9,c.width(labels[i])+4,11);c.color(1);
            c.text(10,baseline,labels[i]);
        }
    }
    c.unclip();
    if(count>3){
        c.box(124,17,1,41);
        c.rounded(123,17+int(lroundf(ui.animation[Selection].value/14*31/(count-1))),3,10,1);
    }
}
void screens::launcher(Canvas& c,const ScreenManager& ui,const Model& m,uint32_t) {
    header(c,"Explore",ui,m);
    const char* labels[]={"Home","Agents","Computer","Network","Focus","Settings"};
    float position=ui.animation[Selection].value/14;
    c.clip(14,46);
    for(int i=0;i<6;++i){
        int x=52+int(lroundf((i-position)*43));
        if(x>-25 && x<128) icons::draw(c,i,x,17);
    }
    c.unclip();c.box(51,44,26,2);
    c.clip(48,59);c.title();
    for(int i=0;i<6;++i){
        int x=64+int(lroundf((i-position)*128));
        if(x>-40 && x<168)c.centeredAt(x,57,labels[i]);
    }
    c.unclip();c.font();
    for(int i=0;i<6;++i)c.box(44+i*8,62,2,1);
    c.box(43+int(lroundf(position*8)),61,4,2);
}
