#pragma once
#include <clib/u8g2.h>
#include "ScreenManager.h"
#include "FrameScheduler.h"
class Renderer {
public:
    explicit Renderer(u8g2_t* display) : display_(display) {}
    void invalidate() { scheduler_.invalidate(); }
    bool render(ScreenManager& ui, const Model& model, uint32_t now);
private:
    void scene(const ScreenManager&, const Model&, uint32_t);
    u8g2_t* display_;
    FrameScheduler scheduler_;
    uint8_t front_[1024]{}, outgoing_[1024]{}, incoming_[1024]{};
    uint32_t routeRevision_ = 0;
};
