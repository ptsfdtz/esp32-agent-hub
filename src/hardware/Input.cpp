#include "Input.h"

uint8_t Input::state() const {
    return (digitalRead(config::EncoderA) << 1) | digitalRead(config::EncoderB);
}
void Input::begin() {
    for (auto pin : {config::EncoderA, config::EncoderB, config::Back,
                     config::Confirm, config::Push}) pinMode(pin, INPUT_PULLUP);
    encoder_.begin(state());
    attachInterruptArg(config::EncoderA, onEdge, this, CHANGE);
    attachInterruptArg(config::EncoderB, onEdge, this, CHANGE);
}
void Input::onEdge(void* context) {
    auto& self = *static_cast<Input*>(context);
    portENTER_CRITICAL_ISR(&self.mux_);
    int turn = self.encoder_.step(self.state(), config::EncoderEdgesPerDetent);
    if (turn) {
        uint8_t next = (self.head_ + 1) % 64;
        if (next == self.tail_) ++self.overflows_;
        else {
            self.turns_[self.head_] = turn * config::EncoderDirection;
            self.head_ = next;
        }
    }
    portEXIT_CRITICAL_ISR(&self.mux_);
}
InputEvent Input::poll(uint32_t now) {
    const uint8_t pins[] = {config::Back, config::Confirm, config::Push};
    for (int i = 0; i < 3; ++i) {
        auto event = buttons_[i].update(digitalRead(pins[i]) == LOW, now);
        if (event != InputEvent::NONE) pending_[i] = event;
    }
    // Buttons cannot be starved by a long stream of encoder edges.
    for (auto& event : pending_) {
        if (event != InputEvent::NONE) {
            auto result = event; event = InputEvent::NONE; return result;
        }
    }
    int turn = 0;
    portENTER_CRITICAL(&mux_);
    if (tail_ != head_) { turn = turns_[tail_]; tail_ = (tail_ + 1) % 64; }
    portEXIT_CRITICAL(&mux_);
    return turn > 0 ? InputEvent::ROTATE_RIGHT :
           turn < 0 ? InputEvent::ROTATE_LEFT : InputEvent::NONE;
}
