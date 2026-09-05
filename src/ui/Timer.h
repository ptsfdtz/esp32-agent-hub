#pragma once
#include <stdint.h>
class Timer {
public:
    uint32_t remainingMs = 25 * 60000;
    bool running = false, complete = false;
    void update(uint32_t now) {
        uint32_t delta = now - last_;
        last_ = now;
        if (!running) return;
        if (delta >= remainingMs) { remainingMs = 0; running = false; complete = true; }
        else remainingMs -= delta;
    }
    void toggle(uint32_t now) {
        update(now);
        if (!remainingMs) remainingMs = 25 * 60000;
        running = !running; complete = false;
    }
    void adjust(int steps) {
        if (running) return;
        int64_t next = int64_t(remainingMs) + int64_t(steps) * 60000;
        remainingMs = next < 60000 ? 60000 : next > 599 * 60000 ? 599 * 60000 : next;
        complete = false;
    }
    void reset() { running = complete = false; remainingMs = 25 * 60000; }
    uint32_t seconds() const { return (remainingMs + 999) / 1000; }
private:
    uint32_t last_ = 0;
};
