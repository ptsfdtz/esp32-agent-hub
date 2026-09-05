#pragma once
#include "Animation.h"
#include "input/InputEvent.h"

enum class Expression { Neutral, Happy, Curious, Wink, Hold };
class Buddy {
public:
    static constexpr uint32_t IdleMs = 20000, SleepMs = 45000;
    Expression expression = Expression::Neutral;
    bool idle = false, sleeping = false;
    void begin(uint32_t now) { lastInput_ = now; }
    // First gesture wakes the panel, without accidentally executing a command.
    bool input(InputEvent event, AnimationManager& a, uint32_t now) {
        bool wake = idle || a[IdleReveal].value > 0;
        lastInput_ = reactionAt_ = now; idle = sleeping = false;
        a.target(IdleReveal, 0, 200, now, true);
        if (a.motion != Motion::Full) { expression = Expression::Neutral; return wake; }
        expression = event == InputEvent::CONFIRM ? Expression::Happy :
            event == InputEvent::PUSH ? Expression::Curious :
            event == InputEvent::BACK || event == InputEvent::BACK_LONG ? Expression::Wink :
            event == InputEvent::PUSH_LONG || event == InputEvent::CONFIRM_LONG ? Expression::Hold : Expression::Neutral;
        reacting_ = true;
        float look = event == InputEvent::ROTATE_LEFT ? -2 : event == InputEvent::ROTATE_RIGHT ? 2 : 0;
        a.target(BuddyLook, look, 150, now, true);
        a.target(BuddyLift, expression == Expression::Happy ? -2 : expression == Expression::Curious ? 1 : 0, 150, now, true);
        a.target(BuddyLid, 0, 100, now, true);
        return wake;
    }
    void wake(AnimationManager& a, uint32_t now) {
        lastInput_ = now; idle = sleeping = false;
        a.target(IdleReveal, 0, 200, now, true);
    }
    bool update(AnimationManager& a, uint32_t now, bool home) {
        auto oldExpression = expression;
        bool oldSleep = sleeping, oldIdle = idle;
        if (a.motion != Motion::Full) {
            expression = Expression::Neutral; idle = sleeping = reacting_ = false;
            a[BuddyLook].snap(0); a[BuddyLift].snap(0); a[BuddyLid].snap(0); a[IdleReveal].snap(0);
        } else {
            uint32_t age = now - lastInput_;
            idle = home && age >= IdleMs;
            sleeping = idle && age >= SleepMs;
            a.target(IdleReveal, idle ? 1 : 0, 200, now, true);
            if (reacting_ && uint32_t(now - reactionAt_) >= 650) reacting_ = false;
            if (!reacting_) {
                expression = Expression::Neutral;
                uint32_t cycle = age % 6000;
                a.target(BuddyLook, idle && !sleeping ? (cycle < 2000 ? -1 : cycle < 4000 ? 1 : 0) : 0, 200, now, true);
                a.target(BuddyLift, idle && (age % 4000) < 2000 ? -1 : 0, 240, now, true);
                a.target(BuddyLid, sleeping || (cycle >= 4700 && cycle < 4900) ? 1 : 0, 100, now, true);
            }
        }
        a.target(BuddySmile, expression==Expression::Happy?1:0,150,now,true);
        a.target(BuddyCurious, expression==Expression::Curious?1:0,150,now,true);
        a.target(BuddyWink, expression==Expression::Wink?1:0,150,now,true);
        a.target(BuddyFocus, expression==Expression::Hold?1:0,150,now,true);
        return oldExpression != expression || oldSleep != sleeping || oldIdle != idle;
    }
private:
    uint32_t lastInput_ = 0, reactionAt_ = 0;
    bool reacting_ = false;
};
