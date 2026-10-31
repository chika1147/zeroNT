#include "include/gui.hpp"
#include "include/classic_ui.hpp"

namespace {
constexpr uint32_t MAX_WINDOWS=16, MAX_RECTS=64, MAX_TEXTS=64, MAX_EVENTS=64;
struct RectCmd { int32_t x,y; uint32_t w,h,rgb; };
struct TextCmd { int32_t x,y; uint32_t rgb; char text[96]; };
struct Window {
    bool used;
    uint32_t handle;
    uint64_t owner;
    int32_t x,y;
    uint32_t w,h;
    char title[40];
    RectCmd rects[MAX_RECTS];
    uint32_t rectCount;
    TextCmd texts[MAX_TEXTS];
    uint32_t textCount;
    GuiEvent events[MAX_EVENTS];
    uint32_t eh,et;
    uint64_t z;
    bool dragging;
    int32_t dragOffX,dragOffY;
};
static ZeroNTBootInfo* gBoot=nullptr;
static Window gWindows[MAX_WINDOWS]{};
static uint32_t gNext=1,gActive=0;
static uint64_t gFrames=0,gEvents=0,gNextZ=1,gFocusChanges=0;
static bool gDirty=false;
static uint32_t gPrevButtons=0;

static bool hit(int32_t x,int32_t y,int32_t l,int32_t t,int32_t r,int32_t b){
    return x>=l&&x<r&&y>=t&&y<b;
}
static void copy_text(char* out,uint32_t cap,const char* in){
    if(!out||!cap)return;uint32_t i=0;if(in)while(i+1<cap&&in[i]){out[i]=in[i];++i;}out[i]=0;
}
static classic_ui::Color color(uint32_t rgb){
    return {static_cast<uint8_t>((rgb>>16)&255),static_cast<uint8_t>((rgb>>8)&255),static_cast<uint8_t>(rgb&255)};
}
static Window* find(uint32_t h){
    for(auto& w:gWindows)if(w.used&&w.handle==h)return &w;
    return nullptr;
}
static Window* top_at(int32_t x,int32_t y){
    Window* best=nullptr;uint64_t bz=0;
    for(auto& w:gWindows){
        if(!w.used||!hit(x,y,w.x,w.y,w.x+static_cast<int32_t>(w.w),w.y+static_cast<int32_t>(w.h)))continue;
        if(!best||w.z>bz){best=&w;bz=w.z;}
    }
    return best;
}
static void post(Window& w,GuiEvent e){
    uint32_t n=(w.eh+1)%MAX_EVENTS;if(n==w.et)return;
    w.events[w.eh]=e;w.eh=n;++gEvents;
}
static void activate(Window* w){
    const uint32_t next=w?w->handle:0;
    if(gActive!=next){gActive=next;++gFocusChanges;gDirty=true;}
    if(w){w->z=gNextZ++;gDirty=true;}
}
static void draw_text_multiline(int x,int y,const char* s,classic_ui::Color c){
    int ox=x,cx=x,cy=y;
    if(!s)return;
    for(uint32_t i=0;s[i];++i){
        const char ch=s[i];
        if(ch=='\r')continue;
        if(ch=='\n'){cx=ox;cy+=10;continue;}
        classic_ui::draw_char(cx,cy,ch,c);
        cx+=8;
    }
}
}

void gui_init(ZeroNTBootInfo* boot){
    gBoot=boot;for(auto&w:gWindows)w={};gNext=1;gActive=0;gFrames=gEvents=gFocusChanges=0;gNextZ=1;gDirty=false;gPrevButtons=0;
}

uint32_t gui_create_window(uint64_t owner,uint32_t width,uint32_t height){
    if(!gBoot||!owner)return 0;
    const uint32_t workW=gBoot->width;
    const uint32_t workH=gBoot->height>28?gBoot->height-28:gBoot->height;
    const uint32_t maxW=workW>24?workW-24:workW;
    const uint32_t maxH=workH>24?workH-24:workH;
    if(width<180)width=180;if(height<120)height=120;
    if(width>maxW)width=maxW;if(height>maxH)height=maxH;
    for(uint32_t i=0;i<MAX_WINDOWS;++i){
        if(gWindows[i].used)continue;
        auto&w=gWindows[i];w={};w.used=true;w.handle=gNext++;w.owner=owner;w.w=width;w.h=height;
        const int32_t centeredX=static_cast<int32_t>((workW-width)/2);
        const int32_t centeredY=static_cast<int32_t>((workH-height)/2);
        w.x=centeredX+static_cast<int32_t>((i%6)*18);w.y=centeredY+static_cast<int32_t>((i%6)*16);
        if(w.x<8)w.x=8;if(w.y<8)w.y=8;
        if(w.x+static_cast<int32_t>(w.w)>static_cast<int32_t>(workW))w.x=static_cast<int32_t>(workW-w.w);
        if(w.y+static_cast<int32_t>(w.h)>static_cast<int32_t>(workH))w.y=static_cast<int32_t>(workH-w.h);
        copy_text(w.title,sizeof(w.title),"zeroNT Application");
        activate(&w);post(w,{GuiEventPaint,0,0,0});gDirty=true;return w.handle;
    }
    return 0;
}

bool gui_close_window(uint32_t h){
    auto*w=find(h);if(!w)return false;
    w->used=false;if(gActive==h)gActive=0;gDirty=true;return true;
}

void gui_close_owner(uint64_t ownerPid){
    for(auto&w:gWindows)if(w.used&&w.owner==ownerPid)w.used=false;
    if(auto*w=find(gActive);!w)gActive=0;
    gDirty=true;
}

bool gui_window_owned_by(uint32_t h,uint64_t ownerPid){auto*w=find(h);return w&&w->owner==ownerPid;}

bool gui_set_title(uint32_t h,const char* title){
    auto*w=find(h);if(!w)return false;copy_text(w->title,sizeof(w->title),title?title:"");gDirty=true;return true;
}

bool gui_begin_paint(uint32_t h){
    auto*w=find(h);if(!w)return false;w->rectCount=0;w->textCount=0;gDirty=true;return true;
}

bool gui_fill_rect(uint32_t h,int32_t x,int32_t y,uint32_t w,uint32_t hh,uint32_t rgb){
    auto*win=find(h);if(!win||win->rectCount>=MAX_RECTS)return false;
    if(x<0||y<0)return false;
    win->rects[win->rectCount++]={x,y,w,hh,rgb};gDirty=true;return true;
}

bool gui_draw_text(uint32_t h,int32_t x,int32_t y,const char* text,uint32_t rgb){
    auto*win=find(h);if(!win||!text||win->textCount>=MAX_TEXTS)return false;
    auto& t=win->texts[win->textCount++];t={};t.x=x;t.y=y;t.rgb=rgb;copy_text(t.text,sizeof(t.text),text);gDirty=true;return true;
}

bool gui_get_event(uint32_t h,GuiEvent&out){
    auto*w=find(h);if(!w||w->et==w->eh){out={};return false;}
    out=w->events[w->et];w->et=(w->et+1)%MAX_EVENTS;return true;
}

bool gui_post_key(uint32_t scancode,uint32_t ascii,uint32_t modifiers){
    auto*w=find(gActive);if(!w)return false;
    post(*w,{GuiEventKey,static_cast<int32_t>(scancode),static_cast<int32_t>(ascii),static_cast<int32_t>(modifiers)});
    return true;
}

bool gui_post_mouse(int32_t x,int32_t y,uint32_t buttons){
    const bool left=(buttons&1u)!=0;
    const bool press=left&&(gPrevButtons&1u)==0;
    const bool release=!left&&(gPrevButtons&1u)!=0;

    Window* active=find(gActive);
    if(active&&active->dragging){
        if(left){
            active->x=x-active->dragOffX;active->y=y-active->dragOffY;
            if(active->x<0)active->x=0;if(active->y<0)active->y=0;
            if(active->x+static_cast<int32_t>(active->w)>static_cast<int32_t>(gBoot->width))
                active->x=static_cast<int32_t>(gBoot->width)-static_cast<int32_t>(active->w);
            if(active->y+static_cast<int32_t>(active->h)>static_cast<int32_t>(gBoot->height)-28)
                active->y=static_cast<int32_t>(gBoot->height)-28-static_cast<int32_t>(active->h);
            gDirty=true;gPrevButtons=buttons;return true;
        }
        active->dragging=false;gDirty=true;gPrevButtons=buttons;return true;
    }

    Window* target=top_at(x,y);
    if(press){
        if(!target){activate(nullptr);gPrevButtons=buttons;return false;}
        activate(target);
        // Close button is handled by the app itself so it can clean up.
        if(hit(x,y,target->x+static_cast<int32_t>(target->w)-22,target->y+4,
               target->x+static_cast<int32_t>(target->w)-5,target->y+20)){
            post(*target,{GuiEventClose,0,0,0});gPrevButtons=buttons;return true;
        }
        if(hit(x,y,target->x,target->y,target->x+static_cast<int32_t>(target->w)-28,target->y+23)){
            target->dragging=true;target->dragOffX=x-target->x;target->dragOffY=y-target->y;
            gPrevButtons=buttons;return true;
        }
    }

    target=find(gActive);
    if(target&&hit(x,y,target->x,target->y,target->x+static_cast<int32_t>(target->w),target->y+static_cast<int32_t>(target->h))){
        const int32_t rx=x-(target->x+2);
        const int32_t ry=y-(target->y+24);
        post(*target,{GuiEventMouse,rx,ry,static_cast<int32_t>(buttons)});
        if(release)gDirty=true;
        gPrevButtons=buttons;return true;
    }
    gPrevButtons=buttons;return false;
}

bool gui_has_active_window(){return find(gActive)!=nullptr;}
uint32_t gui_screen_width(){return gBoot?gBoot->width:0;}
uint32_t gui_screen_height(){return gBoot?gBoot->height:0;}

void gui_on_screen_resize(){
    if(!gBoot)return;
    const uint32_t workW=gBoot->width;
    const uint32_t workH=gBoot->height>28?gBoot->height-28:gBoot->height;
    const uint32_t maxW=workW>24?workW-24:workW;
    const uint32_t maxH=workH>24?workH-24:workH;
    for(auto&w:gWindows){
        if(!w.used)continue;
        if(w.w>maxW)w.w=maxW;if(w.h>maxH)w.h=maxH;
        if(w.w<180&&maxW>=180)w.w=180;if(w.h<120&&maxH>=120)w.h=120;
        if(w.x<0)w.x=0;if(w.y<0)w.y=0;
        if(w.x+static_cast<int32_t>(w.w)>static_cast<int32_t>(workW))w.x=static_cast<int32_t>(workW-w.w);
        if(w.y+static_cast<int32_t>(w.h)>static_cast<int32_t>(workH))w.y=static_cast<int32_t>(workH-w.h);
        if(w.x<0)w.x=0;if(w.y<0)w.y=0;
        w.dragging=false;post(w,{GuiEventPaint,0,0,0});
    }
    gDirty=true;
}

void gui_render(){
    if(!gBoot)return;
    uint64_t last=0;
    for(uint32_t drawn=0;drawn<MAX_WINDOWS;++drawn){
        Window* next=nullptr;uint64_t nz=~0ULL;
        for(auto&w:gWindows)if(w.used&&w.z>last&&w.z<nz){next=&w;nz=w.z;}
        if(!next)break;
        auto&w=*next;
        classic_ui::window_frame({w.x,w.y,static_cast<int>(w.w),static_cast<int>(w.h)},w.title,w.handle==gActive,false,false,true);
        const int contentX=w.x+3,contentY=w.y+25;
        const int contentW=static_cast<int>(w.w)-6,contentH=static_cast<int>(w.h)-28;
        classic_ui::fill({contentX,contentY,contentW,contentH},classic_ui::WHITE);
        for(uint32_t i=0;i<w.rectCount;++i){
            auto&r=w.rects[i];
            int rw=static_cast<int>(r.w),rh=static_cast<int>(r.h);
            if(r.x>=contentW||r.y>=contentH)continue;
            if(r.x+rw>contentW)rw=contentW-r.x;if(r.y+rh>contentH)rh=contentH-r.y;
            if(rw>0&&rh>0)classic_ui::fill({contentX+r.x,contentY+r.y,rw,rh},color(r.rgb));
        }
        for(uint32_t i=0;i<w.textCount;++i){
            auto&t=w.texts[i];draw_text_multiline(contentX+t.x,contentY+t.y,t.text,color(t.rgb));
        }
        last=nz;
    }
    ++gFrames;gDirty=false;
}
bool gui_dirty(){return gDirty;}
void gui_clear_dirty(){gDirty=false;}
GuiStats gui_stats(){GuiStats s{};s.initialized=gBoot!=nullptr;s.active=gActive;s.renderedFrames=gFrames;s.postedEvents=gEvents;s.focusChanges=gFocusChanges;for(auto&w:gWindows)if(w.used)++s.windows;return s;}
