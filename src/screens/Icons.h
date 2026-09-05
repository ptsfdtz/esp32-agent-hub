#pragma once
#include "Canvas.h"
namespace icons {
// Original 24px line icons for the six root pages.
inline void draw(Canvas& c,int id,int x,int y) {
    switch(id) {
        case 0:
            c.line(x+3,y+11,x+12,y+3);c.line(x+12,y+3,x+21,y+11);
            c.line(x+5,y+10,x+5,y+21);c.line(x+19,y+10,x+19,y+21);
            c.line(x+5,y+21,x+19,y+21);c.rounded(x+10,y+14,5,8,1,false);break;
        case 1:
            c.rounded(x+3,y+6,18,14,4,false);c.box(x+7,y+11,3,4);c.box(x+14,y+11,3,4);
            c.line(x+12,y+2,x+12,y+6);c.box(x+11,y+1,3,2);break;
        case 2:
            c.rounded(x+2,y+3,20,14,2,false);c.line(x+12,y+17,x+12,y+21);c.box(x+7,y+21,11,1);
            c.line(x+6,y+8,x+8,y+10);c.line(x+8,y+10,x+6,y+12);c.box(x+11,y+12,6,1);break;
        case 3:
            c.line(x+2,y+7,x+7,y+4);c.box(x+7,y+4,10,1);c.line(x+17,y+4,x+22,y+7);
            c.line(x+6,y+12,x+9,y+10);c.box(x+9,y+10,6,1);c.line(x+15,y+10,x+18,y+12);
            c.rounded(x+10,y+16,5,5,2);break;
        case 4:
            c.rounded(x+4,y+6,17,17,7,false);c.box(x+9,y+1,7,2);c.line(x+12,y+3,x+12,y+6);
            c.line(x+12,y+10,x+12,y+15);c.line(x+12,y+15,x+16,y+15);break;
        case 5:
            for(int i=0;i<3;++i){c.box(x+4,y+5+i*7,17,1);c.rounded(x+(i==1?14:7),y+3+i*7,4,5,1);}break;
    }
}
}
