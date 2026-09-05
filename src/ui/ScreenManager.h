#pragma once
#include "Animation.h"
#include "Buddy.h"
#include "Timer.h"
#include "input/InputEvent.h"
#include "models/Model.h"

enum class Page { Home, Agents, Pc, Iot, Timer, Settings, Launcher,
                  AgentDetail, IotDetail, SettingDetail };
class ScreenManager {
public:
    AnimationManager animation;
    Buddy buddy;
    Timer timer;
    Page page = Page::Home;
    int selected = 0, agent = 0, setting = 0;
    uint32_t routeRevision = 0;
    int direction = 1;
    char toast[22] = "";
    enum class Command { None, Confirm, Cancel, Stop };
    Command commandRequested = Command::None;
    void begin(const Model& model, uint32_t now);
    bool update(const Model& model, uint32_t now);
    void input(InputEvent event, Model& model, uint32_t now);
    void go(Page next, int selection, int dir, uint32_t now);
    int itemCount() const;
    bool isMenu() const;
    void notify(const char* text, uint32_t now);
private:
    void move(int delta, uint32_t now);
    void sync(const Model& model, uint32_t now);
    uint32_t revision_ = UINT32_MAX, second_ = UINT32_MAX, pulse_ = UINT32_MAX;
    uint32_t toastTime_ = 0;
    bool toastClosing_ = false, dirty_ = true;
};
