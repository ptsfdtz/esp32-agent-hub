#pragma once
#include "Canvas.h"
#include "ui/ScreenManager.h"
namespace screens {
void home(Canvas&, const ScreenManager&, const Model&, uint32_t);
void agents(Canvas&, const ScreenManager&, const Model&, uint32_t);
void pc(Canvas&, const ScreenManager&, const Model&, uint32_t);
void iot(Canvas&, const ScreenManager&, const Model&, uint32_t);
void timer(Canvas&, const ScreenManager&, const Model&, uint32_t);
void settings(Canvas&, const ScreenManager&, const Model&, uint32_t);
void launcher(Canvas&, const ScreenManager&, const Model&, uint32_t);
void header(Canvas&, const char*, const ScreenManager&, const Model&);
void menu(Canvas&, const ScreenManager&, const char* const*, int);
}
