#pragma once
#include <stdint.h>
#include "config/Config.h"
class FrameScheduler {
public:
    void invalidate() { dirty_ = true; }
    bool due(uint32_t now) {
        if (!dirty_ || (started_ && uint32_t(now - last_) < config::FrameMs)) return false;
        last_ = now; started_ = true; dirty_ = false; return true;
    }
private:
    uint32_t last_ = 0;
    bool started_ = false, dirty_ = true;
};
