#pragma once
#include <stdint.h>

enum class Easing { Linear, EaseOutCubic, EaseInOutCubic };
enum class Motion { Full, Reduced, Off };
class Animation {
public:
    float value = 0, start = 0, target = 0;
    uint32_t startTime = 0, duration = 0;
    Easing easing = Easing::EaseOutCubic;
    bool running() const { return active_; }
    void snap(float next) { value = start = target = next; active_ = false; }
    void setTarget(float next, uint32_t ms, uint32_t now,
                   Easing curve = Easing::EaseOutCubic) {
        if (next == target && active_) return;
        update(now);
        start = value; target = next; duration = ms; startTime = now; easing = curve;
        active_ = start != target && ms != 0;
        if (!active_) value = target;
    }
    bool update(uint32_t now) {
        if (!active_) return false;
        uint32_t elapsed = now - startTime;
        float t = elapsed >= duration ? 1.0f : float(elapsed) / duration;
        float eased = t;
        if (easing == Easing::EaseOutCubic) { float r = 1 - t; eased = 1 - r*r*r; }
        if (easing == Easing::EaseInOutCubic)
            eased = t < .5f ? 4*t*t*t : 1 - (-2*t+2)*(-2*t+2)*(-2*t+2)/2;
        value = start + (target - start) * eased;
        if (elapsed >= duration) { value = target; active_ = false; }
        return true; // Includes the final frame.
    }
private:
    bool active_ = false;
};

enum Tween { Selection, Scroll, ShortBar, WeekBar, ShortNumber, WeekNumber,
             Cpu, Ram, Gpu, PageSlide, Title, Toast,
             BuddyLook, BuddyLift, BuddyLid, IdleReveal,
             BuddySmile, BuddyCurious, BuddyWink, BuddyFocus, TweenCount };
class AnimationManager {
public:
    Animation tracks[TweenCount];
    Motion motion = Motion::Full;
    Animation& operator[](Tween id) { return tracks[id]; }
    const Animation& operator[](Tween id) const { return tracks[id]; }
    uint32_t time(uint32_t full, bool spatial = false) const {
        return motion == Motion::Off ? 0 :
            motion == Motion::Reduced ? (spatial ? 0 : 100) : full;
    }
    void target(Tween id, float value, uint32_t ms, uint32_t now, bool spatial = false) {
        tracks[id].setTarget(value, time(ms, spatial), now);
    }
    bool update(uint32_t now) {
        bool changed = false;
        for (auto& track : tracks) changed |= track.update(now);
        return changed;
    }
    bool running() const {
        for (const auto& track : tracks) if (track.running()) return true;
        return false;
    }
    void setMotion(Motion next) {
        motion = next;
        for (auto& track : tracks) track.snap(track.target);
    }
};
