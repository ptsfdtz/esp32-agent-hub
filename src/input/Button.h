#pragma once
#include "InputEvent.h"
#include "config/Config.h"

class Button {
public:
    Button(InputEvent shortEvent, InputEvent longEvent)
        : short_(shortEvent), long_(longEvent) {}
    InputEvent update(bool down, uint32_t now) {
        if (down != raw_) { raw_ = down; changed_ = now; }
        if (stable_ != raw_ && uint32_t(now - changed_) >= config::DebounceMs) {
            stable_ = raw_;
            if (stable_) { pressed_ = now; held_ = false; }
            else if (!held_) return short_;
        }
        if (stable_ && !held_ && uint32_t(now - pressed_) >= config::LongPressMs) {
            held_ = true;
            return long_;
        }
        return InputEvent::NONE;
    }
private:
    InputEvent short_, long_;
    bool raw_ = false, stable_ = false, held_ = false;
    uint32_t changed_ = 0, pressed_ = 0;
};
