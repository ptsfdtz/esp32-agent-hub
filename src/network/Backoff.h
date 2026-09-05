#pragma once
#include <stdint.h>
class Backoff {
public:
    bool ready(uint32_t now) const { return !waiting_ || uint32_t(now - started_) >= delay_; }
    void fail(uint32_t now) {
        started_ = now; delay_ = next_; waiting_ = true;
        next_ = next_ < 30000 ? next_ * 2 : 60000;
    }
    void reset() { waiting_ = false; next_ = 1000; }
private:
    bool waiting_ = false;
    uint32_t started_ = 0, delay_ = 0, next_ = 1000;
};
