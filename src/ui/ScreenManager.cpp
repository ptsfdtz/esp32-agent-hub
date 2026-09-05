#include "ScreenManager.h"
#include <stdio.h>

namespace {
int clamp(int n, int low, int high) { return n < low ? low : n > high ? high : n; }
int scrollFor(int selected) { return selected > 2 ? (selected - 2) * 14 : 0; }
}
void ScreenManager::begin(const Model& model, uint32_t now) {
    buddy.begin(now);
    animation[Title].snap(5);
    animation.target(Title, 0, 200, now, true);
    sync(model, now);
}
bool ScreenManager::isMenu() const {
    return page == Page::Launcher || page == Page::Agents || page == Page::Settings;
}
int ScreenManager::itemCount() const { return page == Page::Agents ? 3 : isMenu() ? 6 : 0; }
void ScreenManager::sync(const Model& m, uint32_t now) {
    int index = page == Page::AgentDetail ? agent : 0;
    const auto& a = m.agents[index];
    animation.target(ShortBar, a.shortUsage, 400, now);
    animation.target(WeekBar, a.weeklyUsage, 400, now);
    animation.target(ShortNumber, a.shortUsage, 250, now);
    animation.target(WeekNumber, a.weeklyUsage, 250, now);
    animation.target(Cpu, m.pc.cpu, 250, now);
    animation.target(Ram, m.pc.ram, 250, now);
    animation.target(Gpu, m.pc.gpu, 250, now);
    revision_ = m.revision;
}
bool ScreenManager::update(const Model& m, uint32_t now) {
    uint32_t secondsBefore = timer.seconds();
    bool completedBefore = timer.complete;
    timer.update(now);
    if (timer.complete && !completedBefore) notify("FOCUS COMPLETE", now);
    bool working = fresh(m.agents[0].online, m.agents[0].lastUpdate, now) && m.agents[0].working;
    dirty_ |= buddy.update(animation, now, page == Page::Home && !toast[0] && !working);
    if (page == Page::Timer && secondsBefore != timer.seconds()) dirty_ = true;
    if (revision_ != m.revision) { sync(m, now); dirty_ = true; }
    // Time-dependent screens need at most 1 Hz when all tweens are idle.
    if (second_ != now / 1000) {
        second_ = now / 1000;
        if (page == Page::Home || page == Page::AgentDetail || page == Page::Iot ||
            page == Page::Pc || page == Page::Agents || page == Page::SettingDetail) dirty_ = true;
    }
    if (animation.motion == Motion::Full && working &&
        (page == Page::Home || (page == Page::AgentDetail && agent == 0)) && pulse_ != now / 500) {
        pulse_ = now / 500; dirty_ = true;
    }
    if (toast[0] && !toastClosing_ && uint32_t(now - toastTime_) >= 1600) {
        toastClosing_ = true;
        animation.target(Toast, 0, 180, now, true);
    }
    dirty_ |= animation.update(now);
    if (toastClosing_ && !animation[Toast].running()) {
        toast[0] = 0; toastClosing_ = false; dirty_ = true;
    }
    bool result = dirty_; dirty_ = false; return result;
}
void ScreenManager::go(Page next, int selection, int dir, uint32_t now) {
    if (next == page && selected == selection) return;
    page = next; selected = selection; direction = dir;
    animation[Selection].snap(selected * 14);
    animation[Scroll].snap(scrollFor(selected));
    animation[Title].snap(animation.motion == Motion::Full ? 5 : 0);
    animation.target(Title, 0, 200, now, true);
    revision_ = UINT32_MAX;
    ++routeRevision; dirty_ = true;
}
void ScreenManager::move(int delta, uint32_t now) {
    selected = clamp(selected + delta, 0, itemCount() - 1);
    animation.target(Selection, selected * 14, 150, now, true);
    animation.target(Scroll, scrollFor(selected), 150, now, true);
}
void ScreenManager::notify(const char* text, uint32_t now) {
    buddy.wake(animation, now);
    snprintf(toast, sizeof(toast), "%s", text);
    toastTime_ = now; toastClosing_ = false;
    animation.target(Toast, 1, 180, now, true);
    dirty_ = true;
}
void ScreenManager::input(InputEvent event, Model& m, uint32_t now) {
    if (event == InputEvent::NONE) return;
    dirty_ = true;
    const bool confirm = event == InputEvent::CONFIRM;
    if (buddy.input(event, animation, now)) return;
    const bool push = event == InputEvent::PUSH;
    int turn = event == InputEvent::ROTATE_RIGHT ? 1 : event == InputEvent::ROTATE_LEFT ? -1 : 0;
    if (event == InputEvent::BACK_LONG) { go(Page::Home, 0, -1, now); return; }
    if (event == InputEvent::PUSH_LONG) {
        if (page == Page::AgentDetail) commandRequested = Command::Stop;
        else if (page == Page::Timer) { timer.reset(); notify("TIMER RESET", now); }
        return;
    }
    if (event == InputEvent::CONFIRM_LONG) return;
    if (event == InputEvent::BACK) {
        if (page == Page::AgentDetail) { commandRequested = Command::Cancel; go(Page::Agents, agent, -1, now); }
        else if (page == Page::IotDetail) go(Page::Iot, 0, -1, now);
        else if (page == Page::SettingDetail) go(Page::Settings, setting, -1, now);
        else if (page == Page::Launcher) go(Page::Home, 0, -1, now);
        else if (page != Page::Home) go(Page::Launcher, static_cast<int>(page), -1, now);
        return;
    }
    if (turn) {
        if (isMenu()) move(turn, now);
        else if (page == Page::Timer) timer.adjust(turn);
        else if (page == Page::SettingDetail && setting == 2)
            m.device.contrast = clamp(int(m.device.contrast) + turn * 16, 16, 255);
        else if (page == Page::SettingDetail && setting == 3) {
            int mode = clamp(int(animation.motion) + turn, 0, 2);
            animation.setMotion(static_cast<Motion>(mode));
        } else if (page == Page::Home || page == Page::Pc || page == Page::Iot)
            go(Page::Launcher, clamp(int(page) + turn, 0, 5), 1, now);
        return;
    }
    if (push || !confirm) return;
    switch (page) {
        case Page::Home: go(Page::Launcher, 0, 1, now); break;
        case Page::Launcher: go(static_cast<Page>(selected), 0, 1, now); break;
        case Page::Agents: agent = selected; go(Page::AgentDetail, 0, 1, now); break;
        case Page::Iot: go(Page::IotDetail, 0, 1, now); break;
        case Page::Settings: setting = selected; go(Page::SettingDetail, 0, 1, now); break;
        case Page::Timer: timer.toggle(now); break;
        case Page::AgentDetail: commandRequested = Command::Confirm; break;
        case Page::Pc: go(Page::Launcher, 2, -1, now); break;
        default: break;
    }
}
