#pragma once
#include <Arduino.h>
#include "input/Button.h"
#include "input/Quadrature.h"

class Input {
public:
    void begin();
    InputEvent poll(uint32_t now);
    uint32_t overflowCount() const { return overflows_; }
private:
    static void onEdge(void* context);
    uint8_t state() const;
    Quadrature encoder_;
    // ISR produces; the main loop consumes. Never print or allocate in ISR.
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    int8_t turns_[64]{};
    volatile uint8_t head_ = 0, tail_ = 0;
    volatile uint32_t overflows_ = 0;
    Button buttons_[3] = {
        {InputEvent::BACK, InputEvent::BACK_LONG},
        {InputEvent::CONFIRM, InputEvent::CONFIRM_LONG},
        {InputEvent::PUSH, InputEvent::PUSH_LONG}
    };
    InputEvent pending_[3]{};
};
