#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <filesystem>
#include "input/Quadrature.h"
#include "input/Button.h"
#include "services/MockService.h"
#include "ui/Renderer.h"

namespace fs = std::filesystem;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { fprintf(stderr,"FAIL line %d: %s\n", __LINE__, #condition); return false; } } while(0)

bool coreTests() {
    Quadrature q; q.begin(3);
    CHECK(q.step(1)==0); CHECK(q.step(0)==0); CHECK(q.step(2)==0); CHECK(q.step(3)==1);
    CHECK(q.step(2)==0); CHECK(q.step(0)==0); CHECK(q.step(1)==0); CHECK(q.step(3)==-1);
    q.begin(3);
    for (int i=0; i<100; ++i) { CHECK(q.step(1)==0); CHECK(q.step(3)==0); }
    CHECK(q.step(1)==0); CHECK(q.step(0)==0); CHECK(q.step(2)==0); CHECK(q.step(3)==1);
    q.begin(3); CHECK(q.step(1)==0); CHECK(q.step(2)==0); CHECK(q.step(3)==0);
    q.begin(3);
    for(int i=0;i<1000;++i) { CHECK(q.step(1)==0); CHECK(q.step(0)==0); CHECK(q.step(2)==0); CHECK(q.step(3)==1); }

    Button b(InputEvent::PUSH,InputEvent::PUSH_LONG);
    CHECK(b.update(true,0)==InputEvent::NONE); CHECK(b.update(false,5)==InputEvent::NONE);
    CHECK(b.update(true,10)==InputEvent::NONE); CHECK(b.update(true,30)==InputEvent::NONE);
    CHECK(b.update(false,100)==InputEvent::NONE); CHECK(b.update(false,120)==InputEvent::PUSH);
    CHECK(b.update(true,200)==InputEvent::NONE); CHECK(b.update(true,220)==InputEvent::NONE);
    CHECK(b.update(true,920)==InputEvent::PUSH_LONG); CHECK(b.update(true,1000)==InputEvent::NONE);
    CHECK(b.update(false,1010)==InputEvent::NONE); CHECK(b.update(false,1030)==InputEvent::NONE);
    Button wrapButton(InputEvent::BACK,InputEvent::BACK_LONG);
    wrapButton.update(true,UINT32_MAX-30); wrapButton.update(true,UINT32_MAX-10);
    CHECK(wrapButton.update(true,700)==InputEvent::BACK_LONG);

    Animation a; a.setTarget(100,200,0,Easing::Linear); a.update(100); CHECK(a.value==50);
    a.setTarget(0,100,100); CHECK(a.value==50); a.update(150); CHECK(a.value==6.25f);
    a.update(200); CHECK(a.value==0 && !a.running());
    a.setTarget(100,200,UINT32_MAX-99,Easing::EaseInOutCubic);
    a.update(0); CHECK(a.value==50); a.update(100); CHECK(a.value==100 && !a.running());
    a.setTarget(30,0,100); CHECK(a.value==30 && !a.running());
    AnimationManager animations; animations.setMotion(Motion::Reduced);
    animations.target(Selection,14,150,0,true); CHECK(animations[Selection].value==14);
    animations.target(Cpu,50,250,0); CHECK(animations[Cpu].running());
    animations.setMotion(Motion::Off); CHECK(!animations.running() && animations[Cpu].value==50);

    FrameScheduler scheduler; CHECK(scheduler.due(UINT32_MAX-10));
    scheduler.invalidate(); CHECK(!scheduler.due(10)); CHECK(scheduler.due(22)); CHECK(!scheduler.due(100));
    Timer t; t.update(UINT32_MAX-500); t.toggle(UINT32_MAX-500); t.update(499);
    CHECK(t.remainingMs==1499000); t.toggle(999); CHECK(t.remainingMs==1498500 && !t.running);
    t.update(100000); CHECK(t.remainingMs==1498500);
    t.remainingMs=1000; t.toggle(100000); t.update(101001); CHECK(t.complete && !t.running && t.remainingMs==0);
    t.adjust(-1000); CHECK(t.remainingMs==60000); t.adjust(1000); CHECK(t.remainingMs==599*60000);

    Model m; MockService mock; mock.begin(m,0); ScreenManager ui; ui.begin(m,0);
    ui.input(InputEvent::PUSH,m,500); CHECK(ui.page==Page::Launcher);
    for(int i=0;i<20;++i) ui.input(InputEvent::ROTATE_RIGHT,m,500+i);
    CHECK(ui.selected==5); ui.update(m,1000); CHECK(ui.animation[Scroll].value==42);
    ui.input(InputEvent::CONFIRM,m,1000); CHECK(ui.page==Page::Settings);
    for(int i=0;i<3;++i) ui.input(InputEvent::ROTATE_RIGHT,m,1000+i);
    ui.input(InputEvent::CONFIRM,m,1100); CHECK(ui.page==Page::SettingDetail && ui.setting==3);
    ui.input(InputEvent::ROTATE_RIGHT,m,1200); CHECK(ui.animation.motion==Motion::Reduced);
    ui.input(InputEvent::BACK,m,1300); CHECK(ui.page==Page::Settings && ui.selected==3);
    ui.input(InputEvent::BACK_LONG,m,1400); CHECK(ui.page==Page::Home);
    ui.input(InputEvent::PUSH_LONG,m,1500); CHECK(ui.sampleRequested);
    mock.changeSample(m,1500); ui.update(m,1700); ui.update(m,2000); CHECK(ui.animation[ShortBar].value==80);
    CHECK(fresh(true,UINT32_MAX-100,100)); CHECK(!fresh(true,0,16000));
    Buddy buddy; AnimationManager motion; buddy.begin(UINT32_MAX-1000);
    buddy.update(motion,19000,true); CHECK(buddy.idle); // inactivity survives millis wrap
    motion.update(19300); CHECK(motion[IdleReveal].value==1);
    CHECK(buddy.input(InputEvent::CONFIRM,motion,19400)); // wake consumes confirm
    CHECK(!buddy.idle && buddy.expression==Expression::Happy);
    motion.update(19600); CHECK(motion[IdleReveal].value==0);
    buddy.input(InputEvent::ROTATE_LEFT,motion,19700); motion.update(19850);
    CHECK(motion[BuddyLook].value==-2);
    buddy.input(InputEvent::ROTATE_RIGHT,motion,19850); CHECK(motion[BuddyLook].value==-2);
    motion.update(20000); CHECK(motion[BuddyLook].value==2);
    buddy.update(motion,65000,true); CHECK(buddy.sleeping);
    motion.setMotion(Motion::Reduced); buddy.update(motion,65100,true);
    CHECK(!buddy.idle && !motion.running() && motion[BuddyLid].value==0);
    motion.setMotion(Motion::Off); buddy.input(InputEvent::PUSH,motion,65200);
    buddy.update(motion,90000,true); CHECK(!motion.running() && buddy.expression==Expression::Neutral);
    return true;
}

uint8_t noop(u8x8_t*,uint8_t,uint8_t,void*) { return 1; }
uint8_t physical[1024]{};
uint32_t transferred=0;
u8x8_msg_cb realDisplayCallback;
uint8_t captureDisplay(u8x8_t* d,uint8_t msg,uint8_t count,void* data) {
    if(msg==U8X8_MSG_DISPLAY_DRAW_TILE) {
        auto* tile=static_cast<u8x8_tile_t*>(data);
        for(int i=0;i<count;++i) {
            assert(tile[i].x_pos+tile[i].cnt<=16 && tile[i].y_pos<8);
            memcpy(physical+tile[i].y_pos*128+tile[i].x_pos*8,tile[i].tile_ptr,tile[i].cnt*8);
            transferred+=tile[i].cnt*8;
        }
    }
    return realDisplayCallback(d,msg,count,data);
}
void snapshot(u8g2_t& display, const char* name) {
    fs::create_directories("build/preview");
    char path[160]; snprintf(path,sizeof(path),"build/preview/%s.pbm",name);
    FILE* file=fopen(path,"wb"); assert(file);
    fputs("P1\n128 64\n",file);
    const auto* pixels=u8g2_GetBufferPtr(&display);
    for(int y=0;y<64;++y) { for(int x=0;x<128;++x) fprintf(file,"%d ",(pixels[(y/8)*128+x]>>(y%8))&1); fputc('\n',file); }
    fclose(file);
}
bool renderTests() {
    u8g2_t display;
    u8g2_Setup_sh1106_128x64_noname_f(&display,U8G2_R0,noop,noop);
    u8g2_InitDisplay(&display); u8g2_SetPowerSave(&display,0); u8g2_SetFontMode(&display,1);
    realDisplayCallback=display.u8x8.display_cb;display.u8x8.display_cb=captureDisplay;
    Model m; MockService mock; mock.begin(m,0); ScreenManager ui; ui.begin(m,0); Renderer renderer(&display);
    auto frame=[&](uint32_t now) {
        mock.update(m,now);if(ui.update(m,now)) renderer.invalidate();
        bool rendered=renderer.render(ui,m,now);
        // The emulated OLED must match RAM after every partial/full update,
        // including erased areas, interrupted transitions and blinking.
        assert(memcmp(physical,u8g2_GetBufferPtr(&display),1024)==0);
        return rendered;
    };
    frame(0); frame(500); snapshot(display,"01-home");
    CHECK(u8g2_GetBufferSize(&display)==1024);
    uint8_t before[1024]; memcpy(before,u8g2_GetBufferPtr(&display),1024);
    ui.input(InputEvent::PUSH,m,600); CHECK(frame(600));
    CHECK(memcmp(before,u8g2_GetBufferPtr(&display),1024)==0);
    frame(633); snapshot(display,"02-slide-33ms");
    memcpy(before,u8g2_GetBufferPtr(&display),1024);
    ui.input(InputEvent::BACK,m,640); frame(666);
    CHECK(memcmp(before,u8g2_GetBufferPtr(&display),1024)==0); // interruption continuity
    frame(900); snapshot(display,"03-return-home");
    uint32_t time=1000;
    auto show=[&](Page page,int selection,const char* name) {
        ui.go(page,selection,1,time); frame(time); frame(time+500); snapshot(display,name); time+=1000;
    };
    show(Page::Agents,0,"04-agents");
    auto pixel=[&](int x,int y) { return (u8g2_GetBufferPtr(&display)[y/8*128+x]>>(y%8))&1; };
    CHECK(pixel(119,22)==1); CHECK(pixel(119,36)==0); CHECK(pixel(121,36)==1);
    ui.agent=0; show(Page::AgentDetail,0,"05-agent-detail");
    ui.agent=1; show(Page::Agents,1,"06-agents-second"); show(Page::AgentDetail,0,"07-agent-offline");
    show(Page::Pc,0,"08-pc"); show(Page::Iot,0,"09-iot"); show(Page::IotDetail,0,"10-iot-detail");
    show(Page::Timer,0,"11-timer"); show(Page::Settings,0,"12-settings");
    ui.input(InputEvent::ROTATE_RIGHT,m,time); ui.input(InputEvent::ROTATE_RIGHT,m,time);
    ui.input(InputEvent::ROTATE_RIGHT,m,time); frame(time+33); snapshot(display,"13-menu-moving");
    frame(time+500); snapshot(display,"14-menu-scrolled"); time+=1000;
    show(Page::Settings,5,"15-settings-end");
    for(int setting=0;setting<6;++setting) {
        ui.setting=setting; show(Page::Settings,setting,"settings-temp");
        char name[40]; snprintf(name,sizeof(name),"%02d-setting-detail",16+setting);
        show(Page::SettingDetail,0,name);
    }
    show(Page::Launcher,5,"22-launcher-end");
    CHECK(!frame(time+33)); // static menu does not refresh
    ui.notify("FOCUS COMPLETE",time+100); frame(time+100); frame(time+300); snapshot(display,"23-toast");
    frame(time+1800); frame(time+2100); CHECK(ui.toast[0]==0);
    ui.animation.setMotion(Motion::Off); show(Page::Home,0,"24-motion-off");
    CHECK(!ui.animation.running());
    time += 2000;
    ui.animation.setMotion(Motion::Full); ui.buddy.begin(time);
    frame(time); frame(time+500);
    // Actual renderer frames at 33ms, including idle entrance, breathing,
    // sleep, wake and normal page interactions; no recreated animation code.
    int movieIndex=0;
    auto movie=[&](uint32_t start,int count) {
        for(int i=0;i<count;++i) {
            frame(start+i*33);
            char name[40]; snprintf(name,sizeof(name),"movie-%03d",movieIndex++);
            snapshot(display,name);
        }
    };
    movie(time+19900,190); CHECK(ui.buddy.idle);
    snapshot(display,"25-idle-breathing");
    movie(time+45000,35); CHECK(ui.buddy.sleeping); snapshot(display,"26-idle-sleep");
    ui.input(InputEvent::CONFIRM,m,time+47000);
    CHECK(ui.page==Page::Home && !ui.buddy.idle); // no accidental agent command
    movie(time+47000,25); snapshot(display,"27-wake");
    ui.input(InputEvent::ROTATE_RIGHT,m,time+48000); movie(time+48000,12); snapshot(display,"28-look-right");
    ui.input(InputEvent::CONFIRM,m,time+49000); movie(time+49000,12); snapshot(display,"29-confirm-smile");
    ui.input(InputEvent::BACK,m,time+50000); movie(time+50000,12); snapshot(display,"30-back-wink");
    ui.input(InputEvent::PUSH,m,time+51000); movie(time+51000,12); snapshot(display,"31-push-curious");
    ui.input(InputEvent::BACK_LONG,m,time+52000); movie(time+52000,25);
    uint32_t beforeTransfer=transferred;
    frame(time+53000);
    CHECK(transferred-beforeTransfer<1024);
    return true;
}
int main() {
    if(!coreTests() || !renderTests()) return 1;
    printf("PASS: %d checks; production U8g2 frames exported to build/preview\n",checks);
}
