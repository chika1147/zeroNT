#include "include/classic_ui.hpp"
#include "include/memory.hpp"

namespace classic_ui {
static ZeroNTBootInfo* gBoot = nullptr;
static uint32_t* gBackBuffer = nullptr;
static uint64_t gBackBufferPixels = 0;
static uint32_t gBackBufferPages = 0;
const Color BLACK      = {0,   0,   0};
const Color WHITE      = {255, 255, 255};
const Color DESKTOP    = {0,   128, 128};
const Color FACE       = {192, 192, 192};
const Color HILIGHT    = {255, 255, 255};
const Color LIGHT      = {223, 223, 223};
const Color SHADOW     = {128, 128, 128};
const Color DARKSHADOW = {0,   0,   0};
const Color ACTIVEBLUE = {0,   0,   128};
const Color INACTIVE   = {128, 128, 128};

void init(ZeroNTBootInfo* boot) { gBoot = boot; }
bool enable_backbuffer() {
    if (!gBoot || !gBoot->framebuffer || !gBoot->width || !gBoot->height) return false;
    if (gBackBuffer) return true;
    const uint64_t pixels = static_cast<uint64_t>(gBoot->width) * static_cast<uint64_t>(gBoot->height);
    if (!pixels || pixels > (64ULL * 1024ULL * 1024ULL) / sizeof(uint32_t)) return false;
    const uint64_t bytes = pixels * sizeof(uint32_t);
    const uint64_t pages64 = (bytes + 4095ULL) / 4096ULL;
    if (!pages64 || pages64 > 0xFFFFFFFFULL) return false;

    // Keep the framebuffer-sized backbuffer outside the small general kernel heap.
    // PMM returns identity-mapped pages below 4 GiB in the current v33 memory model,
    // so 1080p/1200p modes no longer consume half of the 16 MiB heap.
    gBackBufferPages = static_cast<uint32_t>(pages64);
    gBackBuffer = static_cast<uint32_t*>(pmm_alloc_pages(gBackBufferPages, 1));
    if (!gBackBuffer) { gBackBufferPages = 0; return false; }
    gBackBufferPixels = pixels;

    // Preserve whatever the loader/kernel already displayed so switching to
    // buffered rendering never produces a flash or an uninitialized frame.
    auto* hw = reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(gBoot->framebuffer));
    for (uint32_t y = 0; y < gBoot->height; ++y) {
        const uint64_t srcRow = static_cast<uint64_t>(y) * gBoot->pixelsPerScanLine;
        const uint64_t dstRow = static_cast<uint64_t>(y) * gBoot->width;
        for (uint32_t x = 0; x < gBoot->width; ++x) gBackBuffer[dstRow + x] = hw[srcRow + x];
    }
    return true;
}
bool backbuffer_enabled() { return gBackBuffer != nullptr; }

bool reconfigure_backbuffer() {
    if (!gBoot || !gBoot->framebuffer || !gBoot->width || !gBoot->height) return false;
    const uint64_t pixels = static_cast<uint64_t>(gBoot->width) * static_cast<uint64_t>(gBoot->height);
    if (!pixels || pixels > (128ULL * 1024ULL * 1024ULL) / sizeof(uint32_t)) return false;
    const uint64_t bytes = pixels * sizeof(uint32_t);
    const uint64_t pages64 = (bytes + 4095ULL) / 4096ULL;
    if (!pages64 || pages64 > 0xFFFFFFFFULL) return false;
    const uint32_t newPages = static_cast<uint32_t>(pages64);
    auto* next = static_cast<uint32_t*>(pmm_alloc_pages(newPages, 1));
    if (!next) {
        if (gBackBuffer) pmm_free_pages(gBackBuffer, gBackBufferPages);
        gBackBuffer=nullptr;gBackBufferPages=0;gBackBufferPixels=0;
        return false;
    }
    if (gBackBuffer) pmm_free_pages(gBackBuffer, gBackBufferPages);
    gBackBuffer=next;gBackBufferPages=newPages;gBackBufferPixels=pixels;
    // New video modes may contain stale VRAM. Start with the classic desktop
    // color and let the compositor redraw all windows on the next frame.
    const uint32_t bg=pack(DESKTOP);
    for(uint64_t i=0;i<pixels;++i)gBackBuffer[i]=bg;
    return true;
}

void present_rect(Rect r) {
    if (!gBoot || !gBoot->framebuffer || !gBackBuffer) return;
    int x0 = r.x < 0 ? 0 : r.x;
    int y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w;
    int y1 = r.y + r.h;
    if (x1 > static_cast<int>(gBoot->width)) x1 = static_cast<int>(gBoot->width);
    if (y1 > static_cast<int>(gBoot->height)) y1 = static_cast<int>(gBoot->height);
    if (x0 >= x1 || y0 >= y1) return;
    auto* hw = reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(gBoot->framebuffer));
    for (int y = y0; y < y1; ++y) {
        const uint64_t srcRow = static_cast<uint64_t>(y) * gBoot->width;
        const uint64_t dstRow = static_cast<uint64_t>(y) * gBoot->pixelsPerScanLine;
        for (int x = x0; x < x1; ++x) hw[dstRow + static_cast<uint32_t>(x)] = gBackBuffer[srcRow + static_cast<uint32_t>(x)];
    }
}
void present() {
    if (!gBoot || !gBoot->framebuffer || !gBackBuffer) return;
    present_rect({0, 0, static_cast<int>(gBoot->width), static_cast<int>(gBoot->height)});
}
uint32_t pack(Color c) {
    if (!gBoot) return 0;
    if (gBoot->pixelFormat == 0)
        return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16);
    return (uint32_t)c.b | ((uint32_t)c.g << 8) | ((uint32_t)c.r << 16);
}
void pixel(int x, int y, Color c) {
    if (!gBoot || !gBoot->framebuffer || x < 0 || y < 0 || x >= (int)gBoot->width || y >= (int)gBoot->height) return;
    if (gBackBuffer) {
        const uint64_t i = static_cast<uint64_t>(y) * gBoot->width + static_cast<uint32_t>(x);
        if (i < gBackBufferPixels) gBackBuffer[i] = pack(c);
        return;
    }
    auto* fb = (volatile uint32_t*)(uintptr_t)gBoot->framebuffer;
    fb[(uint64_t)y * gBoot->pixelsPerScanLine + (uint32_t)x] = pack(c);
}
void fill(Rect r, Color c) {
    int x0=r.x<0?0:r.x, y0=r.y<0?0:r.y;
    int x1=r.x+r.w, y1=r.y+r.h;
    if (!gBoot) return;
    if (x1>(int)gBoot->width) x1=(int)gBoot->width;
    if (y1>(int)gBoot->height) y1=(int)gBoot->height;
    for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) pixel(x,y,c);
}
void hline(int x1,int x2,int y,Color c){if(x2<x1){int t=x1;x1=x2;x2=t;}for(int x=x1;x<=x2;++x)pixel(x,y,c);}
void vline(int x,int y1,int y2,Color c){if(y2<y1){int t=y1;y1=y2;y2=t;}for(int y=y1;y<=y2;++y)pixel(x,y,c);}
void frame3d(Rect r,bool sunken){
    Color top1=sunken?SHADOW:HILIGHT, top2=sunken?DARKSHADOW:LIGHT;
    Color bot1=sunken?LIGHT:SHADOW, bot2=sunken?HILIGHT:DARKSHADOW;
    hline(r.x,r.x+r.w-1,r.y,top1); vline(r.x,r.y,r.y+r.h-1,top1);
    hline(r.x+1,r.x+r.w-2,r.y+1,top2); vline(r.x+1,r.y+1,r.y+r.h-2,top2);
    hline(r.x,r.x+r.w-1,r.y+r.h-1,bot2); vline(r.x+r.w-1,r.y,r.y+r.h-1,bot2);
    hline(r.x+1,r.x+r.w-2,r.y+r.h-2,bot1); vline(r.x+r.w-2,r.y+1,r.y+r.h-2,bot1);
}

static const uint8_t kFont5x7[95][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, //  32  
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, //  33 !
    {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}, //  34 quote
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, //  35 #
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, //  36 $
    {0x19,0x1A,0x04,0x08,0x16,0x06,0x00}, //  37 %
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, //  38 &
    {0x04,0x04,0x02,0x00,0x00,0x00,0x00}, //  39 apostrophe
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, //  40 (
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, //  41 )
    {0x00,0x11,0x0A,0x04,0x0A,0x11,0x00}, //  42 *
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, //  43 +
    {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, //  44 ,
    {0x00,0x00,0x00,0x0E,0x00,0x00,0x00}, //  45 -
    {0x00,0x00,0x00,0x00,0x00,0x04,0x04}, //  46 .
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, //  47 /
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, //  48 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, //  49 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, //  50 2
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, //  51 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, //  52 4
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}, //  53 5
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, //  54 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, //  55 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, //  56 8
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}, //  57 9
    {0x00,0x04,0x04,0x00,0x04,0x04,0x00}, //  58 :
    {0x00,0x04,0x04,0x00,0x04,0x04,0x08}, //  59 ;
    {0x01,0x02,0x04,0x08,0x04,0x02,0x01}, //  60 <
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, //  61 =
    {0x10,0x08,0x04,0x02,0x04,0x08,0x10}, //  62 >
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, //  63 ?
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, //  64 @
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, //  65 A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, //  66 B
    {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}, //  67 C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, //  68 D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, //  69 E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, //  70 F
    {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F}, //  71 G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, //  72 H
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, //  73 I
    {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}, //  74 J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, //  75 K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, //  76 L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, //  77 M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, //  78 N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, //  79 O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, //  80 P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, //  81 Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, //  82 R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, //  83 S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, //  84 T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, //  85 U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, //  86 V
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, //  87 W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, //  88 X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, //  89 Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, //  90 Z
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, //  91 [
    {0x10,0x08,0x08,0x04,0x02,0x02,0x01}, //  92 backslash
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, //  93 ]
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, //  94 ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, //  95 _
    {0x08,0x04,0x02,0x00,0x00,0x00,0x00}, //  96 `
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, //  97 a
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, //  98 b
    {0x00,0x00,0x0F,0x10,0x10,0x10,0x0F}, //  99 c
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, // 100 d
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0F}, // 101 e
    {0x06,0x08,0x1E,0x08,0x08,0x08,0x08}, // 102 f
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, // 103 g
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, // 104 h
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 105 i
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 106 j
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 107 k
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 108 l
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, // 109 m
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, // 110 n
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 111 o
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // 112 p
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, // 113 q
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 114 r
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, // 115 s
    {0x08,0x08,0x1E,0x08,0x08,0x09,0x06}, // 116 t
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // 117 u
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 118 v
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, // 119 w
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 120 x
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 121 y
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 122 z
    {0x03,0x04,0x04,0x08,0x04,0x04,0x03}, // 123 {
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, // 124 |
    {0x18,0x04,0x04,0x02,0x04,0x04,0x18}, // 125 }
    {0x00,0x00,0x09,0x16,0x00,0x00,0x00}, // 126 ~
};
static uint8_t glyph(char ch,int row){
    if(row<0||row>=7)return 0; unsigned int c=(unsigned char)ch;
    if(c>=32&&c<=126)return kFont5x7[c-32][row];
    static const uint8_t missing[7]={0x1F,0x11,0x11,0x11,0x11,0x11,0x1F}; return missing[row];
}
void draw_char(int x,int y,char ch,Color c,int scale){for(int row=0;row<7;++row){uint8_t bits=glyph(ch,row);for(int col=0;col<5;++col)if((bits>>(4-col))&1)fill({x+col*scale,y+row*scale,scale,scale},c);}}
void text(int x,int y,const char*s,Color c,int scale){if(!s)return;while(*s){draw_char(x,y,*s,c,scale);x+=6*scale;++s;}}
int text_width(const char*s,int scale){int n=0;while(s&&*s++){++n;}return n*6*scale;}
void button(Rect r,const char*label,bool pressed,bool disabled){
    fill(r,FACE); frame3d(r,pressed);
    int ox=pressed?1:0, oy=pressed?1:0; Color fg=disabled?SHADOW:BLACK;
    if(disabled){text(r.x+(r.w-text_width(label))/2+1,r.y+(r.h-7)/2+1,label,HILIGHT);}
    text(r.x+(r.w-text_width(label))/2+ox,r.y+(r.h-7)/2+oy,label,fg);
}
void draw_window_icon(int x,int y){fill({x,y,16,16},BLACK);fill({x+1,y+1,14,14},FACE);fill({x+2,y+2,12,3},ACTIVEBLUE);fill({x+3,y+6,10,7},WHITE);}
void window_frame(Rect r,const char*title,bool active,bool showMin,bool showMax,bool showClose){
    fill(r,FACE); frame3d(r,false); Rect tr={r.x+3,r.y+3,r.w-6,18}; fill(tr,active?ACTIVEBLUE:INACTIVE); draw_window_icon(tr.x+2,tr.y+1); text(tr.x+21,tr.y+5,title,WHITE);
    int right=r.x+r.w-6;
    if(showClose){Rect b={right-16,r.y+5,16,14};button(b,"",false);for(int i=0;i<6;++i){pixel(b.x+5+i,b.y+4+i,BLACK);pixel(b.x+10-i,b.y+4+i,BLACK);}right-=18;}
    if(showMax){Rect b={right-16,r.y+5,16,14};button(b,"",false);hline(b.x+4,b.x+11,b.y+4,BLACK);hline(b.x+4,b.x+11,b.y+5,BLACK);vline(b.x+4,b.y+4,b.y+10,BLACK);vline(b.x+11,b.y+4,b.y+10,BLACK);hline(b.x+4,b.x+11,b.y+10,BLACK);right-=18;}
    if(showMin){Rect b={right-16,r.y+5,16,14};button(b,"",false);hline(b.x+4,b.x+10,b.y+9,BLACK);hline(b.x+4,b.x+10,b.y+10,BLACK);}
}
void draw_computer_icon(int x,int y){fill({x+2,y,25,17},SHADOW);fill({x+3,y+1,23,15},WHITE);fill({x+5,y+3,19,11},DESKTOP);fill({x+11,y+17,7,3},FACE);fill({x+7,y+20,15,3},FACE);frame3d({x+7,y+20,15,3},false);}
void draw_folder_icon(int x,int y){Color yellow={255,255,0};fill({x+2,y+5,26,17},yellow);fill({x+4,y+2,10,5},yellow);hline(x+2,x+27,y+5,BLACK);vline(x+2,y+5,y+21,BLACK);hline(x+2,x+27,y+21,BLACK);vline(x+27,y+5,y+21,BLACK);}
void draw_network_icon(int x,int y){fill({x+1,y+2,12,9},SHADOW);fill({x+2,y+3,10,7},WHITE);fill({x+5,y+11,4,3},FACE);fill({x+17,y+11,12,9},SHADOW);fill({x+18,y+12,10,7},WHITE);fill({x+21,y+20,4,3},FACE);hline(x+8,x+22,y+16,BLACK);vline(x+8,y+13,y+16,BLACK);}
void draw_trash_icon(int x,int y){fill({x+7,y+4,15,18},WHITE);vline(x+7,y+4,y+21,BLACK);vline(x+21,y+4,y+21,BLACK);hline(x+7,x+21,y+21,BLACK);hline(x+5,x+23,y+4,BLACK);hline(x+10,x+18,y+2,BLACK);vline(x+11,y+7,y+18,SHADOW);vline(x+15,y+7,y+18,SHADOW);vline(x+19,y+7,y+18,SHADOW);}
void draw_start_mark(int x,int y){fill({x,y,16,14},SHADOW);fill({x+1,y+1,14,12},FACE);fill({x+3,y+2,10,10},ACTIVEBLUE);fill({x+5,y+4,6,6},WHITE);fill({x+7,y+5,2,4},ACTIVEBLUE);}
void draw_document_icon(int x,int y){fill({x+5,y+1,20,24},WHITE);frame3d({x+5,y+1,20,24},false);hline(x+9,x+21,y+8,SHADOW);hline(x+9,x+21,y+12,SHADOW);hline(x+9,x+18,y+16,SHADOW);}
void draw_control_icon(int x,int y){fill({x+3,y+3,24,20},FACE);frame3d({x+3,y+3,24,20},false);fill({x+8,y+8,14,10},ACTIVEBLUE);vline(x+15,y+5,y+21,BLACK);hline(x+5,x+25,y+13,BLACK);}
void draw_mouse(int x,int y){for(int i=0;i<13;++i)for(int j=0;j<=i/2;++j)pixel(x+j,y+i,WHITE);vline(x,y,y+13,BLACK);for(int i=0;i<8;++i)pixel(x+i,y+i*2,BLACK);hline(x,x+5,y+12,BLACK);vline(x+4,y+9,y+16,BLACK);hline(x+4,x+7,y+16,BLACK);vline(x+7,y+14,y+16,BLACK);}


static void hw_pixel(int x,int y,Color c){
    if(!gBoot||!gBoot->framebuffer||x<0||y<0||x>=(int)gBoot->width||y>=(int)gBoot->height)return;
    auto* fb=reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(gBoot->framebuffer));
    fb[(uint64_t)y*gBoot->pixelsPerScanLine+(uint32_t)x]=pack(c);
}
static void hw_hline(int x1,int x2,int y,Color c){if(x2<x1){int t=x1;x1=x2;x2=t;}for(int x=x1;x<=x2;++x)hw_pixel(x,y,c);}
static void hw_vline(int x,int y1,int y2,Color c){if(y2<y1){int t=y1;y1=y2;y2=t;}for(int y=y1;y<=y2;++y)hw_pixel(x,y,c);}
static void draw_mouse_hw(int x,int y){
    for(int i=0;i<13;++i)for(int j=0;j<=i/2;++j)hw_pixel(x+j,y+i,WHITE);
    hw_vline(x,y,y+13,BLACK);
    for(int i=0;i<8;++i)hw_pixel(x+i,y+i*2,BLACK);
    hw_hline(x,x+5,y+12,BLACK);
    hw_vline(x+4,y+9,y+16,BLACK);
    hw_hline(x+4,x+7,y+16,BLACK);
    hw_vline(x+7,y+14,y+16,BLACK);
}
void present_mouse(int x,int y){
    if(gBackBuffer) draw_mouse_hw(x,y);
    else draw_mouse(x,y);
}
void move_mouse(int oldX,int oldY,int newX,int newY){
    if(!gBackBuffer){draw_mouse(newX,newY);return;}
    // Restore the old pointer from the cursor-free backbuffer, then paint only
    // the new cursor directly to the hardware framebuffer.  The +2 margin
    // covers every pixel of the 8x17 classic arrow even at screen edges.
    present_rect({oldX-1,oldY-1,11,20});
    draw_mouse_hw(newX,newY);
}
}
