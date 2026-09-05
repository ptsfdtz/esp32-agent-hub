#pragma once
#include <stdint.h>

// Opposite-corner transitions mean a missed/noisy edge: discard the partial
// detent. Legal contact bounce cancels itself rather than inventing a turn.
class Quadrature {
public:
    void begin(uint8_t state) { previous_ = state & 3; partial_ = 0; }
    int step(uint8_t state, int edges = 4) {
        static const int8_t table[16] = {
            0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0
        };
        state &= 3;
        if ((previous_ ^ state) == 3) partial_ = 0;
        else partial_ += table[(previous_ << 2) | state];
        previous_ = state;
        if (partial_ >= edges) { partial_ = 0; return 1; }
        if (partial_ <= -edges) { partial_ = 0; return -1; }
        return 0;
    }
private:
    uint8_t previous_ = 3;
    int8_t partial_ = 0;
};
