#pragma once
#include <stdint.h>
#include "../../shared/bootinfo.hpp"

enum GuiEventType : uint32_t {
    GuiEventNone=0,
    GuiEventKey=1,
    GuiEventMouse=2,
    GuiEventClose=3,
    GuiEventPaint=4
};
struct GuiEvent { uint32_t type; int32_t a,b,c; };
struct GuiStats {
    bool initialized;
    uint32_t windows;
    uint32_t active;
    uint64_t renderedFrames;
    uint64_t postedEvents;
    uint64_t focusChanges;
};

void gui_init(ZeroNTBootInfo* boot);
uint32_t gui_create_window(uint64_t ownerPid, uint32_t width, uint32_t height);
bool gui_close_window(uint32_t handle);
void gui_close_owner(uint64_t ownerPid);
bool gui_window_owned_by(uint32_t handle, uint64_t ownerPid);
bool gui_set_title(uint32_t handle,const char* title);
bool gui_begin_paint(uint32_t handle);
bool gui_fill_rect(uint32_t handle, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t rgb);
bool gui_draw_text(uint32_t handle,int32_t x,int32_t y,const char* text,uint32_t rgb);
bool gui_get_event(uint32_t handle, GuiEvent& out);

// Returns true when a user-mode window consumed the input.  Shell/desktop
// input must only run when these return false.
bool gui_post_key(uint32_t scancode,uint32_t ascii,uint32_t modifiers);
bool gui_post_mouse(int32_t x,int32_t y,uint32_t buttons);
bool gui_has_active_window();
uint32_t gui_screen_width();
uint32_t gui_screen_height();
// Clamp/repaint all user-mode windows after a runtime resolution change.
void gui_on_screen_resize();

void gui_render();
bool gui_dirty();
void gui_clear_dirty();
GuiStats gui_stats();
