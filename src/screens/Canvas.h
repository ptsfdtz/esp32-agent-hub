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
