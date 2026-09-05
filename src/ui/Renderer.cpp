#include "Renderer.h"
#include "screens/Screens.h"
#include "screens/BuddyDrawing.h"
#include <string.h>

void Renderer::scene(const ScreenManager& ui, const Model& m, uint32_t now) {
    u8g2_ClearBuffer(display_);
    Canvas c(display_); c.font(); c.color(1); c.unclip();
    switch (ui.page) {
        case Page::Home: screens::home(c,ui,m,now); break;
        case Page::Agents: case Page::AgentDetail: screens::agents(c,ui,m,now); break;
        case Page::Pc: screens::pc(c,ui,m,now); break;
        case Page::Iot: case Page::IotDetail: screens::iot(c,ui,m,now); break;
        case Page::Timer: screens::timer(c,ui,m,now); break;
        case Page::Settings: case Page::SettingDetail: screens::settings(c,ui,m,now); break;
        case Page::Launcher: screens::launcher(c,ui,m,now); break;
    }
    if (ui.page == Page::Home) buddyDrawing::standby(c,ui,m,now);
    if (ui.toast[0]) {
        int y = 64 - int(lroundf(ui.animation[Toast].value * 16));
        c.color(0); c.box(0,y,128,16); c.color(1); c.box(0,y,128,1);
        if (y < 64) c.text(2,y+12,ui.toast);
    }
}
bool Renderer::render(ScreenManager& ui, const Model& m, uint32_t now) {
    if (ui.animation.running() || routeRevision_ != ui.routeRevision) scheduler_.invalidate();
    if (!scheduler_.due(now)) return false;
    if (routeRevision_ != ui.routeRevision) {
        // Snapshot the last PRESENTED composite, including interrupted slides.
        memcpy(outgoing_, front_, sizeof(front_));
        ui.animation[PageSlide].snap(0);
        ui.animation.target(PageSlide, 128, 200, now, true);
        routeRevision_ = ui.routeRevision;
    }
    scene(ui, m, now);
    uint8_t* buffer = u8g2_GetBufferPtr(display_);
    if (ui.animation[PageSlide].running()) {
        memcpy(incoming_, buffer, sizeof(incoming_));
        int shift = int(lroundf(ui.animation[PageSlide].value));
        // SH1106 full buffer: eight tile rows, 128 vertical-byte columns.
        // Horizontal slides are byte copies, not 8192 per-pixel operations.
        for (int row=0; row<8; ++row) for (int x=0; x<128; ++x) {
            int oldX = x + ui.direction * shift;
            int newX = x - ui.direction * (128-shift);
            buffer[row*128+x] = oldX >= 0 && oldX < 128 ? outgoing_[row*128+oldX] :
                                 newX >= 0 && newX < 128 ? incoming_[row*128+newX] : 0;
        }
    }
    // SH1106 tile rows: send only changed column spans on sparse updates.
    // Large transitions retain the efficient single full-buffer path.
    uint8_t first[8], last[8];
    int changedTiles=0;
    for(int row=0;row<8;++row) {
        first[row]=16;last[row]=0;
        for(int tile=0;tile<16;++tile) {
            int offset=row*128+tile*8;
            if(memcmp(front_+offset,buffer+offset,8)!=0) {
                if(first[row]==16)first[row]=tile;
                last[row]=tile;
            }
        }
        if(first[row]!=16)changedTiles+=last[row]-first[row]+1;
    }
    if(changedTiles>80)u8g2_SendBuffer(display_);
    else for(int row=0;row<8;++row)
        if(first[row]!=16)u8g2_UpdateDisplayArea(display_,first[row],row,last[row]-first[row]+1,1);
    memcpy(front_, buffer, sizeof(front_));
    return true;
}
