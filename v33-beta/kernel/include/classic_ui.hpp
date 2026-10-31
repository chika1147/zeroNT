#pragma once
#include "../../shared/bootinfo.hpp"
#include <stdint.h>

namespace classic_ui {
struct Rect { int x, y, w, h; };
struct Color { uint8_t r, g, b; };

extern const Color BLACK;
extern const Color WHITE;
extern const Color DESKTOP;
extern const Color FACE;
extern const Color HILIGHT;
extern const Color LIGHT;
extern const Color SHADOW;
extern const Color DARKSHADOW;
extern const Color ACTIVEBLUE;
extern const Color INACTIVE;

void init(ZeroNTBootInfo* boot);
// Enable the off-screen framebuffer after the kernel heap is available.
// All drawing then happens in RAM first and is copied to the hardware
// framebuffer only when present()/present_rect() is called.  This keeps all
// background processing intact while preventing half-drawn classic windows
// from becoming visible on QEMU/real hardware.
bool enable_backbuffer();
bool backbuffer_enabled();
// Reallocate the off-screen buffer after a runtime display mode change.
bool reconfigure_backbuffer();
void present();
void present_rect(Rect r);
uint32_t pack(Color c);
void pixel(int x, int y, Color c);
void fill(Rect r, Color c);
void hline(int x1, int x2, int y, Color c);
void vline(int x, int y1, int y2, Color c);
void frame3d(Rect r, bool sunken);
void draw_char(int x, int y, char ch, Color c, int scale = 1);
void text(int x, int y, const char* s, Color c, int scale = 1);
int text_width(const char* s, int scale = 1);
void button(Rect r, const char* label, bool pressed = false, bool disabled = false);
void window_frame(Rect r, const char* title, bool active = true, bool showMin = true, bool showMax = true, bool showClose = true);
void draw_window_icon(int x, int y);
void draw_computer_icon(int x, int y);
void draw_folder_icon(int x, int y);
void draw_network_icon(int x, int y);
void draw_trash_icon(int x, int y);
void draw_start_mark(int x, int y);
void draw_document_icon(int x, int y);
void draw_control_icon(int x, int y);
void draw_mouse(int x, int y);
// Hardware-cursor helpers: the backbuffer stays cursor-free, so pointer motion
// can update only a tiny rectangle instead of presenting the whole screen.
void present_mouse(int x, int y);
void move_mouse(int oldX, int oldY, int newX, int newY);
}
