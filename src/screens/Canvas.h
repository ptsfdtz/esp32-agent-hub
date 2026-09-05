#pragma once
#include <clib/u8g2.h>
#include <stdio.h>
#include <math.h>
#include "ui/Typography.h"

// All screens draw into RAM only. Transport is exclusively Renderer-owned.
class Canvas {
public:
    explicit Canvas(u8g2_t* target) : target_(target) {}
    void font(bool large = false) { u8g2_SetFont(target_, large ? typography::number() : typography::body()); }
    void title() { u8g2_SetFont(target_, typography::title()); }
    void small() { u8g2_SetFont(target_, typography::small()); }
    int width(const char* text) { return u8g2_GetStrWidth(target_, text); }
    void centeredAt(int x,int y,const char* text) { this->text(x-width(text)/2,y,text); }
    void line(int x,int y,int xx,int yy) {
        int dx=abs(xx-x), sx=x<xx?1:-1, dy=-abs(yy-y), sy=y<yy?1:-1, err=dx+dy;
        for (;;) { box(x,y,1,1); if(x==xx && y==yy) break;
            int e=2*err; if(e>=dy){err+=dy;x+=sx;} if(e<=dx){err+=dx;y+=sy;}}
    }
    // Signed clipping permits icons to enter from outside the OLED edges.
    void rounded(int x,int y,int w,int h,int r,bool fill=true) {
        if(w<=0 || h<=0) return;
        r = r>w/2?w/2:r; r=r>h/2?h/2:r;
        for(int row=0;row<h;++row) {
            int edge=row<r?r-row-1:row>=h-r?row-(h-r):0;
            int inset=r ? r-int(sqrtf(float(r*r-edge*edge))) : 0;
            if(fill || row==0 || row==h-1) box(x+inset,y+row,w-2*inset,1);
            else {box(x+inset,y+row,1,1);box(x+w-inset-1,y+row,1,1);}
        }
    }
    void meter(int x,int y,int w,float percent) {
        box(x,y+2,w,1);
        int fill=int(lroundf(w*percent/100)); fill=fill<0?0:fill>w?w:fill;
        rounded(x,y,fill,4,2);
    }
    void center(int y, const char* text) { this->text((128 - int(u8g2_GetStrWidth(target_, text)))/2, y, text); }
    void text(int x, int y, const char* text) { u8g2_DrawStr(target_, x, y, text); }
    void right(int y, const char* text) { this->text(126 - u8g2_GetStrWidth(target_, text), y, text); }
    void clippedText(int x, int y, const char* text, int chars = 20) {
        char buffer[22];
        snprintf(buffer, sizeof(buffer), "%.*s", chars, text);
        this->text(x, y, buffer);
    }
    void box(int x, int y, int w, int h) {
        int right = x + w, bottom = y + h;
        x = x < 0 ? 0 : x; y = y < 0 ? 0 : y;
        right = right > 128 ? 128 : right; bottom = bottom > 64 ? 64 : bottom;
        if (right > x && bottom > y) u8g2_DrawBox(target_, x, y, right-x, bottom-y);
    }
    void color(int color) { u8g2_SetDrawColor(target_, color); }
    void clip(int y, int bottom) { u8g2_SetClipWindow(target_, 0, y, 128, bottom); }
    void unclip() { u8g2_SetMaxClipWindow(target_); }
    void dot(int x, int y, bool on, int radius = 2) {
        if (on) u8g2_DrawDisc(target_, x, y, radius, U8G2_DRAW_ALL);
        else u8g2_DrawCircle(target_, x, y, 2, U8G2_DRAW_ALL);
    }
    void percent(int y, float value) {
        char buffer[8]; snprintf(buffer, sizeof(buffer), "%d%%", int(lroundf(value))); right(y, buffer);
    }
    void bar(int y, float value) {
        u8g2_DrawFrame(target_, 2, y, 124, 5);
        int width = int(lroundf(value * 120 / 100));
        box(4, y+2, width < 0 ? 0 : width > 120 ? 120 : width, 1);
    }
private:
    u8g2_t* target_;
};
