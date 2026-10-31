#include "../shared/bootinfo.hpp"
#include "include/arch.hpp"
#include "include/memory.hpp"
#include "include/interrupts.hpp"
#include "include/paging.hpp"
#include "include/heap.hpp"
#include "include/block_device.hpp"
#include "include/fat32.hpp"
#include "include/pci.hpp"
#include "include/ahci.hpp"
#include "include/scheduler.hpp"
#include "include/process.hpp"
#include "include/usermode.hpp"
#include "include/syscall.hpp"
#include "include/executable.hpp"
#include "include/gui.hpp"
#include "include/driver_manager.hpp"
#include "include/nvme.hpp"
#include "include/usb.hpp"
#include "include/acpi.hpp"
#include "include/platform_hardware.hpp"
#include "include/display.hpp"
#include "include/audio.hpp"
#include "include/network.hpp"
#include "include/user_manager.hpp"
#include "include/system_apps.hpp"
#include "include/quality.hpp"
#include "include/security.hpp"
#include "include/safe_fs.hpp"
#include "include/event_log.hpp"
#include "include/beta.hpp"
#include "include/installer.hpp"
#include "include/classic_ui.hpp"
#include <stdint.h>

static ZeroNTBootInfo* gBoot = nullptr;
static volatile uint64_t gWorkerACounter = 0;
static volatile uint64_t gWorkerBCounter = 0;

[[maybe_unused]] static void scheduler_demo_worker(void* arg) {
    auto* counter = static_cast<volatile uint64_t*>(arg);
    for (;;) {
        ++(*counter);
        __asm__ __volatile__("pause");
    }
}

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!gBoot) return 0;
    if (gBoot->pixelFormat == 0)
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16);
    return static_cast<uint32_t>(b) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(r) << 16);
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!gBoot || !gBoot->framebuffer || x >= gBoot->width || y >= gBoot->height) return;
    auto* fb = reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(gBoot->framebuffer));
    fb[static_cast<uint64_t>(y) * gBoot->pixelsPerScanLine + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!gBoot) return;
    uint32_t maxX = x + w; if (maxX > gBoot->width) maxX = gBoot->width;
    uint32_t maxY = y + h; if (maxY > gBoot->height) maxY = gBoot->height;
    for (uint32_t yy = y; yy < maxY; ++yy)
        for (uint32_t xx = x; xx < maxX; ++xx)
            put_pixel(xx, yy, color);
}

static void glyph_rows(char ch, uint8_t out[7]) {
    for (int i = 0; i < 7; ++i) out[i] = 0;
    switch (ch) {
        case ' ': break;
        case '-': out[3]=0x1F; break;
        case '*': out[1]=0x15; out[2]=0x0E; out[3]=0x1F; out[4]=0x0E; out[5]=0x15; break;
        case '.': out[6]=0x04; break;
        case ':': out[2]=0x04; out[5]=0x04; break;
        case '/': out[0]=0x10;out[1]=0x08;out[2]=0x08;out[3]=0x04;out[4]=0x02;out[5]=0x02;out[6]=0x01;break;
        case '\\': out[0]=0x01;out[1]=0x02;out[2]=0x02;out[3]=0x04;out[4]=0x08;out[5]=0x08;out[6]=0x10;break;
        case '>': out[1]=0x02;out[2]=0x04;out[3]=0x08;out[4]=0x04;out[5]=0x02;break;
        case '<': out[1]=0x08;out[2]=0x04;out[3]=0x02;out[4]=0x04;out[5]=0x08;break;
        case '_': out[6]=0x1F; break;
        case '0': {uint8_t a[7]={0x0E,0x11,0x19,0x15,0x13,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case '1': {uint8_t a[7]={0x04,0x06,0x04,0x04,0x04,0x04,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case '2': {uint8_t a[7]={0x0E,0x11,0x10,0x08,0x04,0x02,0x1F}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case '3': {uint8_t a[7]={0x0E,0x11,0x10,0x0C,0x10,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case '4': {uint8_t a[7]={0x08,0x0C,0x0A,0x09,0x1F,0x08,0x08}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case '5': {uint8_t a[7]={0x1F,0x01,0x0F,0x10,0x10,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case '6': {uint8_t a[7]={0x0C,0x02,0x01,0x0F,0x11,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case '7': {uint8_t a[7]={0x1F,0x10,0x08,0x04,0x02,0x02,0x02}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case '8': {uint8_t a[7]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case '9': {uint8_t a[7]={0x0E,0x11,0x11,0x1E,0x10,0x08,0x06}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'A': {uint8_t a[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'B': {uint8_t a[7]={0x0F,0x11,0x11,0x0F,0x11,0x11,0x0F}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'C': {uint8_t a[7]={0x0E,0x11,0x01,0x01,0x01,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'D': {uint8_t a[7]={0x0F,0x11,0x11,0x11,0x11,0x11,0x0F}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'E': {uint8_t a[7]={0x1F,0x01,0x01,0x0F,0x01,0x01,0x1F}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'F': {uint8_t a[7]={0x1F,0x01,0x01,0x0F,0x01,0x01,0x01}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'G': {uint8_t a[7]={0x0E,0x11,0x01,0x1D,0x11,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'H': {uint8_t a[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'I': {uint8_t a[7]={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'J': {uint8_t a[7]={0x1C,0x08,0x08,0x08,0x08,0x09,0x06}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'K': {uint8_t a[7]={0x11,0x09,0x05,0x03,0x05,0x09,0x11}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'L': {uint8_t a[7]={0x01,0x01,0x01,0x01,0x01,0x01,0x1F}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'M': {uint8_t a[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'N': {uint8_t a[7]={0x11,0x13,0x15,0x19,0x11,0x11,0x11}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'O': {uint8_t a[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'P': {uint8_t a[7]={0x0F,0x11,0x11,0x0F,0x01,0x01,0x01}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'Q': {uint8_t a[7]={0x0E,0x11,0x11,0x11,0x15,0x09,0x16}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'R': {uint8_t a[7]={0x0F,0x11,0x11,0x0F,0x05,0x09,0x11}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'S': {uint8_t a[7]={0x0E,0x11,0x01,0x0E,0x10,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'T': {uint8_t a[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'U': {uint8_t a[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'V': {uint8_t a[7]={0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'W': {uint8_t a[7]={0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'X': {uint8_t a[7]={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'Y': {uint8_t a[7]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        case 'Z': {uint8_t a[7]={0x1F,0x10,0x08,0x04,0x02,0x01,0x1F}; for(int i=0;i<7;++i)out[i]=a[i];} break;
        default: {uint8_t a[7]={0x1F,0x11,0x15,0x15,0x15,0x11,0x1F}; for(int i=0;i<7;++i)out[i]=a[i];} break;
    }
}

static void draw_char(uint32_t x, uint32_t y, char ch, uint32_t fg, uint32_t scale=2) {
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    uint8_t rows[7]; glyph_rows(ch, rows);
    for (uint32_t yy=0; yy<7; ++yy)
        for (uint32_t xx=0; xx<5; ++xx)
            if (rows[yy] & (1u << xx)) fill_rect(x + xx*scale, y + yy*scale, scale, scale, fg);
}

static void draw_text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t scale=2) {
    uint32_t cx=x;
    while (*s) { draw_char(cx,y,*s,fg,scale); cx += 6*scale; ++s; }
}

static uint32_t gEarlyBootY = 24;
static uint32_t gEarlyBootPercent = 0;

// Pace only the visible startup progress.  All real boot initialization still
// runs in the same order; we simply expose it in calm 100 ms steps so the
// original zeroNT splash does not flash past instantly on fast machines.
// PIT channel 2 is used as a polling one-shot, so this works before IRQ0/timer
// scheduling is enabled and does not consume the main PIT channel 0.
static void boot_wait_50ms() {
    constexpr uint16_t count = 59659; // 1,193,182 Hz * 0.050 s
    const uint8_t old61 = inb(0x61);

    // Gate channel 2 low while programming it. Keep the speaker data bit off.
    outb(0x61, static_cast<uint8_t>(old61 & ~0x03u));
    outb(0x43, 0xB0); // channel 2, lo/hi byte, mode 0, binary
    outb(0x42, static_cast<uint8_t>(count & 0xFFu));
    outb(0x42, static_cast<uint8_t>((count >> 8) & 0xFFu));
    outb(0x61, static_cast<uint8_t>((old61 & ~0x02u) | 0x01u));

    // OUT2 (bit 5) goes low during the count and high at terminal count.
    // Timeouts make this pacing cosmetic rather than something that can hang boot.
    for (uint32_t guard=0; guard<1000000u && (inb(0x61) & 0x20u); ++guard)
        __asm__ __volatile__("pause");
    for (uint32_t guard=0; guard<8000000u && !(inb(0x61) & 0x20u); ++guard)
        __asm__ __volatile__("pause");

    outb(0x61, old61);
}

static void boot_wait_100ms() {
    boot_wait_50ms();
    boot_wait_50ms();
}

static void early_boot_progress(uint32_t percent) {
    if (!gBoot) return;
    if (percent > 100) percent = 100;
    const uint32_t black = pack_rgb(0,0,0);
    const uint32_t blue  = pack_rgb(0,0,128);
    const int cx=(int)gBoot->width/2, cy=(int)gBoot->height/2-40;
    const int bx=cx-110, by=cy+102, bw=220, bh=14;
    const int innerX=bx+4, innerY=by+4, innerW=bw-8, innerH=bh-8;
    fill_rect(innerX, innerY, innerW, innerH, black);
    const int filled=(innerW*(int)percent)/100;
    if (filled>0) fill_rect(innerX, innerY, (uint32_t)filled, innerH, blue);
}

static void early_boot_begin() {
    // Original zeroNT startup presentation: keep all boot work silent behind a
    // stable black splash instead of showing kernel diagnostics.
    const uint32_t black = pack_rgb(0,0,0);
    const uint32_t white = pack_rgb(255,255,255);
    const uint32_t face  = pack_rgb(192,192,192);
    const uint32_t blue  = pack_rgb(0,0,128);
    const uint32_t gray  = pack_rgb(128,128,128);
    fill_rect(0,0,gBoot->width,gBoot->height,black);
    const int cx=(int)gBoot->width/2, cy=(int)gBoot->height/2-40;
    const int x=cx-112, y=cy-28;
    fill_rect(x,y,68,68,face);
    fill_rect(x+3,y+3,62,62,gray);
    fill_rect(x+5,y+5,58,58,face);
    fill_rect(x+9,y+9,50,50,blue);
    fill_rect(x+19,y+16,30,36,white);
    fill_rect(x+26,y+23,16,22,blue);
    draw_text(cx-28,cy-18,"zeroNT",white,2);
    draw_text(cx-26,cy+10,"WORKSTATION",white,1);
    draw_text(cx-64,cy+76,"STARTING ZERONT...",face,1);
    const int bx=cx-110, by=cy+102, bw=220, bh=14;
    fill_rect(bx,by,bw,bh,face);
    fill_rect(bx+2,by+2,bw-4,bh-4,white);
    fill_rect(bx+4,by+4,bw-8,bh-8,black);
    gEarlyBootPercent = 0;
    early_boot_progress(gEarlyBootPercent);
    gEarlyBootY = 24;
}
static void early_boot_stage(const char*) {
    // Keep diagnostics hidden, but advance the original boot bar by 5% and
    // hold each visible step for 100 ms. Processing itself is not removed.
    if (gEarlyBootPercent < 95) gEarlyBootPercent += 5;
    early_boot_progress(gEarlyBootPercent);
    boot_wait_100ms();
}

static void early_boot_finish() {
    while (gEarlyBootPercent < 100) {
        uint32_t next = gEarlyBootPercent + 5;
        if (next > 100) next = 100;
        gEarlyBootPercent = next;
        early_boot_progress(gEarlyBootPercent);
        boot_wait_100ms();
    }
}

static void draw_u64(uint32_t x, uint32_t y, uint64_t value, uint32_t fg, uint32_t scale=1) {
    char buf[24]; int n=0;
    if (value==0) buf[n++]='0';
    else {
        char rev[24]; int r=0;
        while(value && r<23){ rev[r++]=static_cast<char>('0'+(value%10)); value/=10; }
        while(r) buf[n++]=rev[--r];
    }
    buf[n]=0;
    draw_text(x,y,buf,fg,scale);
}

static void draw_hex64(uint32_t x, uint32_t y, uint64_t value, uint32_t fg, uint32_t scale=1) {
    char buf[19];
    buf[0]='0'; buf[1]='X';
    static const char hex[]="0123456789ABCDEF";
    for (int i=0;i<16;++i) {
        const int shift=(15-i)*4;
        buf[2+i]=hex[(value>>shift)&0xF];
    }
    buf[18]=0;
    draw_text(x,y,buf,fg,scale);
}

static void draw_ipv4(uint32_t x, uint32_t y, const uint8_t ip[4], uint32_t fg) {
    uint32_t cx=x;
    for (int i=0;i<4;++i) {
        draw_u64(cx,y,ip[i],fg,1);
        cx += ip[i] >= 100 ? 24 : (ip[i] >= 10 ? 18 : 12);
        if (i != 3) { draw_text(cx,y,".",fg,1); cx += 8; }
    }
}

static char scan_to_ascii(uint8_t sc, bool shift) {
    static const char normal[128] = {
        0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,'\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
        'd','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v',
        'b','n','m',',','.','/',0,'*',0,' ',0
    };
    static const char shifted[128] = {
        0,27,'!','@','#','$','%','^','&','*','(',')','_','+',8,'\t',
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
        'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
        'B','N','M','<','>','?',0,'*',0,' ',0
    };
    if (sc >= 128) return 0;
    return shift ? shifted[sc] : normal[sc];
}

static void append_text(char* dst, uint32_t cap, const char* src) {
    if (!dst || !cap || !src) return;
    uint32_t n=0; while (n+1<cap && dst[n]) ++n;
    for (uint32_t i=0; src[i] && n+1<cap; ++i) dst[n++]=src[i];
    dst[n]=0;
}

static void root_list_callback(const Fat32DirEntry& entry, void* context) {
    auto* dst = static_cast<char*>(context);
    if (!dst) return;
    append_text(dst, 160, entry.name);
    if (entry.attributes & 0x10) append_text(dst, 160, "/");
    append_text(dst, 160, "  ");
}

static void sanitize_preview(char* text, uint32_t cap) {
    if (!text || !cap) return;
    for (uint32_t i=0; i+1<cap && text[i]; ++i) {
        const unsigned char c=static_cast<unsigned char>(text[i]);
        if (c=='\r' || c=='\n' || c=='\t') text[i]=' ';
        else if (c < 32 || c > 126) text[i]='?';
    }
}

[[maybe_unused]] static void draw_screen(const char* typed, uint32_t typedLen, int mouseX, int mouseY,
                        uint8_t mouseButtons, void* testPage, void* heapTest,
                        const Fat32Volume& fat, bool fatMounted,
                        const char* rootListing, const char* readmePreview,
                        const AhciStats& ahci, bool usingAhci, bool writeTestOk, const char* writePreview,
                        const AudioStats& audio, const NetworkStats& net) {
    const uint32_t teal = pack_rgb(0, 92, 92);
    const uint32_t gray = pack_rgb(192,192,192);
    const uint32_t white = pack_rgb(255,255,255);
    const uint32_t black = pack_rgb(0,0,0);
    const uint32_t blue = pack_rgb(0,0,128);
    const uint32_t dark = pack_rgb(96,96,96);

    fill_rect(0,0,gBoot->width,gBoot->height,teal);
    const uint32_t winW = gBoot->width > 780 ? 760 : gBoot->width - 20;
    const uint32_t winH = gBoot->height > 660 ? 640 : gBoot->height - 20;
    const uint32_t wx = (gBoot->width-winW)/2;
    const uint32_t wy = (gBoot->height-winH)/2;
    fill_rect(wx,wy,winW,winH,gray);
    fill_rect(wx,wy,winW,28,blue);
    fill_rect(wx+winW-2,wy,2,winH,dark);
    fill_rect(wx,wy+winH-2,winW,2,dark);
    draw_text(wx+10,wy+7,"ZERONT V32 INTEGRATED INSTALLER",white,2);

    UserSystemStats account = user_system_stats();
    PmmStats mem = pmm_stats();
    PagingStats paging = vmm_stats();
    HeapStats heap = heap_stats();
    uint32_t y=wy+44;
    draw_text(wx+16,y,"USER:",black,1);
    draw_text(wx+58,y,account.loggedIn ? account.displayName : "(NOT LOGGED IN)",black,1);
    draw_text(wx+250,y,"UID:",black,1); draw_u64(wx+286,y,account.currentUid,black,1);
    draw_text(wx+360,y,"ROLE:",black,1);
    draw_text(wx+406,y,account.currentRole==UserRole::Administrator ? "ADMIN" : (account.currentRole==UserRole::Standard ? "STANDARD" : "GUEST"),black,1); y+=18;
    draw_text(wx+16,y,"HOME:",black,1); draw_text(wx+58,y,account.loggedIn ? account.logicalHome : "-",black,1);
    draw_text(wx+430,y,"MOUSE UI",black,1); y+=20;
    draw_text(wx+16,y,"STORAGE:",black,1);
    draw_text(wx+82,y,usingAhci ? "AHCI SATA DISK" : "RAMDISK FALLBACK",black,1);
    draw_text(wx+230,y,"PCI:",black,1);
    draw_u64(wx+270,y,ahci.bus,black,1); draw_text(wx+282,y,":",black,1);
    draw_u64(wx+290,y,ahci.device,black,1); draw_text(wx+302,y,".",black,1);
    draw_u64(wx+310,y,ahci.function,black,1);
    draw_text(wx+350,y,"PORT:",black,1); draw_u64(wx+398,y,ahci.port,black,1);
    y+=18;

    draw_text(wx+16,y,"AHCI ABAR:",black,1); draw_hex64(wx+108,y,ahci.abar,black,1);
    draw_text(wx+300,y,"SECTORS:",black,1); draw_u64(wx+372,y,ahci.sectors,black,1); y+=18;

    draw_text(wx+16,y,"FAT32 MOUNT:",black,1);
    draw_text(wx+118,y,fatMounted ? "OK" : "FAILED",black,1);
    if (fatMounted) {
        draw_text(wx+200,y,"ROOT CLUSTER:",black,1); draw_u64(wx+302,y,fat.stats.rootCluster,black,1);
        draw_text(wx+370,y,"DATA CLUSTERS:",black,1); draw_u64(wx+490,y,fat.stats.dataClusters,black,1);
    }
    y+=18;

    draw_text(wx+16,y,"ROOT DIR:",black,1); draw_text(wx+88,y,rootListing && *rootListing ? rootListing : "(EMPTY)",black,1); y+=18;
    draw_text(wx+16,y,"README.TXT:",black,1); draw_text(wx+112,y,readmePreview && *readmePreview ? readmePreview : "(READ FAILED)",black,1); y+=18;
    draw_text(wx+16,y,"FAT32 WRITE TEST:",black,1); draw_text(wx+154,y,writeTestOk ? "OK" : "FAILED",black,1);
    draw_text(wx+230,y,"INSTALLER:",black,1); draw_text(wx+306,y,"BUILT IN",black,1); y+=20;

    draw_text(wx+16,y,"AUDIO:",black,1); draw_text(wx+72,y,audio.initialized ? "AC97 READY" : "NOT FOUND",black,1);
    draw_text(wx+190,y,"TONES:",black,1); draw_u64(wx+246,y,audio.tonesPlayed,black,1);
    draw_text(wx+330,y,"NETWORK:",black,1); draw_text(wx+404,y,net.initialized ? (net.linkUp ? "E1000 LINK" : "E1000 NO LINK") : "NOT FOUND",black,1); y+=18;
    draw_text(wx+16,y,"IP:",black,1); draw_ipv4(wx+42,y,net.ipv4,black);
    draw_text(wx+180,y,"GW:",black,1); draw_ipv4(wx+206,y,net.gateway,black);
    draw_text(wx+350,y,"DHCP:",black,1); draw_text(wx+400,y,net.dhcpBound ? "BOUND" : "STATIC/FALLBACK",black,1); y+=18;
    draw_text(wx+16,y,"NET TX:",black,1); draw_u64(wx+76,y,net.txPackets,black,1);
    draw_text(wx+160,y,"RX:",black,1); draw_u64(wx+190,y,net.rxPackets,black,1);
    draw_text(wx+270,y,"ARP:",black,1); draw_u64(wx+306,y,net.arpReplies,black,1);
    draw_text(wx+370,y,"PING REPLY:",black,1); draw_u64(wx+462,y,net.icmpReplies,black,1); y+=20;

    draw_text(wx+16,y,"CR3:",black,1); draw_hex64(wx+60,y,paging.cr3,black,1);
    draw_text(wx+230,y,"FREE MEM KB:",black,1); draw_u64(wx+330,y,mem.freeBytes/1024,black,1);
    draw_text(wx+430,y,"HEAP FREE KB:",black,1); draw_u64(wx+540,y,heap.freeBytes/1024,black,1); y+=18;
    draw_text(wx+16,y,"PMM TEST:",black,1); draw_hex64(wx+94,y,reinterpret_cast<uint64_t>(testPage),black,1);
    draw_text(wx+270,y,"HEAP TEST:",black,1); draw_hex64(wx+354,y,reinterpret_cast<uint64_t>(heapTest),black,1); y+=18;
    draw_text(wx+16,y,"TIMER TICKS:",black,1); draw_u64(wx+112,y,timer_ticks(),black,1); y+=18;

    SchedulerStats sched = scheduler_stats();
    ProcessStats procs = process_stats();
    draw_text(wx+16,y,"SCHED TID:",black,1); draw_u64(wx+100,y,sched.currentTid,black,1);
    draw_text(wx+150,y,"PID:",black,1); draw_u64(wx+188,y,sched.currentPid,black,1);
    draw_text(wx+240,y,"THREADS:",black,1); draw_u64(wx+310,y,sched.threadCount,black,1);
    draw_text(wx+370,y,"PROCESSES:",black,1); draw_u64(wx+456,y,procs.processCount,black,1); y+=18;
    draw_text(wx+16,y,"CONTEXT SWITCHES:",black,1); draw_u64(wx+154,y,sched.contextSwitches,black,1);
    draw_text(wx+280,y,"PREEMPTIONS:",black,1); draw_u64(wx+382,y,sched.preemptions,black,1); y+=18;
    draw_text(wx+16,y,"WORKER A:",black,1); draw_u64(wx+92,y,gWorkerACounter,black,1);
    draw_text(wx+280,y,"WORKER B:",black,1); draw_u64(wx+356,y,gWorkerBCounter,black,1); y+=22;

    draw_text(wx+16,y,"KEYBOARD IRQ INPUT:",black,1); y+=18;
    fill_rect(wx+16,y,winW-32,38,white);
    char visible[81];
    uint32_t start = typedLen > 80 ? typedLen-80 : 0;
    uint32_t n=0;
    for (uint32_t i=start; i<typedLen && n<80; ++i) visible[n++]=typed[i];
    visible[n]=0;
    draw_text(wx+22,y+11,visible,black,1); y+=56;

    draw_text(wx+16,y,"MOUSE IRQ X:",black,1); draw_u64(wx+112,y,static_cast<uint64_t>(mouseX),black,1);
    draw_text(wx+190,y,"Y:",black,1); draw_u64(wx+210,y,static_cast<uint64_t>(mouseY),black,1);
    draw_text(wx+290,y,"BUTTONS:",black,1); draw_u64(wx+360,y,mouseButtons,black,1); y+=22;
    draw_text(wx+16,y,usingAhci ? "FAT32 READ/WRITE IS DIRECT ON THE AHCI SATA DISK." : "AHCI FAILED - WRITES ARE RAMDISK-ONLY.",black,1);

    if (mouseX >= 0 && mouseY >= 0) {
        for (int i=0;i<12;++i) {
            put_pixel(static_cast<uint32_t>(mouseX+i), static_cast<uint32_t>(mouseY), black);
            put_pixel(static_cast<uint32_t>(mouseX), static_cast<uint32_t>(mouseY+i), black);
        }
    }
 }

enum class AuthStage : uint8_t {
    SetupName,
    SetupPassword,
    SetupConfirm,
    LoginName,
    LoginPassword,
    Session,
};

static void text_clear(char* p, uint32_t cap) { if (!p||!cap)return; for(uint32_t i=0;i<cap;++i)p[i]=0; }
static uint32_t text_length(const char* p) { uint32_t n=0; if(p) while(p[n]) ++n; return n; }
static void text_backspace(char* p) { uint32_t n=text_length(p); if(n) p[n-1]=0; }
static bool text_push(char* p,uint32_t cap,char c) { uint32_t n=text_length(p); if(!p||n+1>=cap)return false; p[n]=c;p[n+1]=0;return true; }
static bool text_same(const char*a,const char*b){if(!a||!b)return false;for(uint32_t i=0;;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}}
static void text_set(char*d,uint32_t cap,const char*s){if(!d||!cap)return;uint32_t i=0;if(s)while(i+1<cap&&s[i]){d[i]=s[i];++i;}d[i]=0;}

static void draw_auth_screen(AuthStage stage,const char* name,const char* password,const char* message) {
    const bool setup=(stage==AuthStage::SetupName||stage==AuthStage::SetupPassword||stage==AuthStage::SetupConfirm);
    if(!setup){
        // Match the original backup logon UI: solid blue desktop and a compact
        // classic dialog with user/password/domain fields.
        classic_ui::fill({0,0,(int)gBoot->width,(int)gBoot->height},classic_ui::ACTIVEBLUE);
        const int w=410,h=225;
        const int x=((int)gBoot->width-w)/2, y=((int)gBoot->height-h)/2;
        classic_ui::window_frame({x,y,w,h},"LOG ON TO ZERONT",true,false,false,false);
        classic_ui::draw_computer_icon(x+24,y+42);
        classic_ui::text(x+66,y+42,"WELCOME TO ZERONT",classic_ui::BLACK);
        classic_ui::text(x+66,y+58,"TYPE YOUR NAME AND PASSWORD TO LOG ON.",classic_ui::BLACK);
        classic_ui::text(x+42,y+94,"USER NAME:",classic_ui::BLACK);
        classic_ui::fill({x+132,y+87,220,22},classic_ui::WHITE);classic_ui::frame3d({x+132,y+87,220,22},true);
        classic_ui::text(x+137,y+94,name,classic_ui::BLACK);
        if(stage==AuthStage::LoginName)classic_ui::vline(x+137+classic_ui::text_width(name),y+92,y+101,classic_ui::BLACK);
        classic_ui::text(x+42,y+124,"PASSWORD:",classic_ui::BLACK);
        classic_ui::fill({x+132,y+117,220,22},classic_ui::WHITE);classic_ui::frame3d({x+132,y+117,220,22},true);
        char masked[64]{};uint32_t n=text_length(password);if(n>63)n=63;for(uint32_t i=0;i<n;++i)masked[i]='*';
        classic_ui::text(x+137,y+124,masked,classic_ui::BLACK);
        if(stage==AuthStage::LoginPassword)classic_ui::vline(x+137+classic_ui::text_width(masked),y+122,y+131,classic_ui::BLACK);
        classic_ui::text(x+42,y+154,"DOMAIN:",classic_ui::BLACK);
        classic_ui::fill({x+132,y+147,220,22},classic_ui::WHITE);classic_ui::frame3d({x+132,y+147,220,22},true);
        classic_ui::text(x+137,y+154,"SELFMADE",classic_ui::BLACK);
        if(message&&*message)classic_ui::text(x+18,y+178,message,{128,0,0});
        classic_ui::button({x+126,y+184,72,24},"OK",false,false);
        classic_ui::button({x+204,y+184,72,24},"Cancel",false,true);
        classic_ui::button({x+282,y+184,72,24},"Help",false,false);
        classic_ui::present();
        return;
    }

    // First-time account creation remains integrated, but uses the same classic
    // controls/colors as the original shell.
    classic_ui::fill({0,0,(int)gBoot->width,(int)gBoot->height},classic_ui::DESKTOP);
    const int w=430,h=268;
    const int x=((int)gBoot->width-w)/2, y=((int)gBoot->height-h)/2;
    classic_ui::window_frame({x,y,w,h},"zeroNT Setup",true,false,false,false);
    classic_ui::draw_computer_icon(x+22,y+42);
    classic_ui::text(x+66,y+42,"WELCOME TO ZERONT SETUP",classic_ui::BLACK);
    classic_ui::text(x+66,y+58,"CREATE THE ACCOUNT USED ON THIS COMPUTER.",classic_ui::BLACK);
    classic_ui::text(x+24,y+82,"SETUP STEP 2 OF 2",classic_ui::ACTIVEBLUE);
    if(stage==AuthStage::SetupName){
        classic_ui::text(x+42,y+114,"USER NAME:",classic_ui::BLACK);
        classic_ui::fill({x+132,y+107,250,22},classic_ui::WHITE);classic_ui::frame3d({x+132,y+107,250,22},true);
        classic_ui::text(x+137,y+114,name,classic_ui::BLACK);classic_ui::vline(x+137+classic_ui::text_width(name),y+112,y+121,classic_ui::BLACK);
        classic_ui::text(x+42,y+146,"CHOOSE ANY NAME YOU LIKE.",classic_ui::BLACK);
    }else{
        classic_ui::text(x+42,y+109,"USER NAME:",classic_ui::BLACK);classic_ui::text(x+132,y+109,name,classic_ui::BLACK);
        classic_ui::text(x+42,y+139,stage==AuthStage::SetupConfirm?"CONFIRM:":"PASSWORD:",classic_ui::BLACK);
        classic_ui::fill({x+132,y+132,250,22},classic_ui::WHITE);classic_ui::frame3d({x+132,y+132,250,22},true);
        char masked[64]{};uint32_t n=text_length(password);if(n>63)n=63;for(uint32_t i=0;i<n;++i)masked[i]='*';
        classic_ui::text(x+137,y+139,masked,classic_ui::BLACK);classic_ui::vline(x+137+classic_ui::text_width(masked),y+137,y+146,classic_ui::BLACK);
        classic_ui::text(x+42,y+170,"PASSWORD CHARACTERS ARE HIDDEN.",classic_ui::BLACK);
    }
    if(message&&*message)classic_ui::text(x+24,y+196,message,{128,0,0});
    classic_ui::hline(x+8,x+w-9,y+h-52,classic_ui::SHADOW);classic_ui::hline(x+8,x+w-9,y+h-51,classic_ui::HILIGHT);
    classic_ui::button({x+w-176,y+h-40,72,24},stage==AuthStage::SetupConfirm?"Finish":"Next >",false,false);
    classic_ui::button({x+w-96,y+h-40,72,24},"Cancel",false,true);
    classic_ui::present();
}

static void draw_installer_screen(uint64_t copied,uint64_t total,const char* status,bool error=false) {
    classic_ui::fill({0,0,(int)gBoot->width,(int)gBoot->height},classic_ui::DESKTOP);
    const int w=(gBoot->width>820?780:(int)gBoot->width-30), h=390;
    const int x=((int)gBoot->width-w)/2, y=((int)gBoot->height-h)/2;
    classic_ui::window_frame({x,y,w,h},"zeroNT Setup",true,false,false,false);
    classic_ui::fill({x+4,y+24,w-8,86},classic_ui::WHITE);
    classic_ui::text(x+24,y+42,error?"Setup could not continue":"Installing zeroNT",error?classic_ui::Color{160,0,0}:classic_ui::ACTIVEBLUE,2);
    classic_ui::text(x+24,y+72,error?"Check the target disk and try again.":"Please wait while Setup installs zeroNT on your computer.",classic_ui::BLACK);
    classic_ui::hline(x+4,x+w-5,y+109,classic_ui::SHADOW); classic_ui::hline(x+4,x+w-5,y+110,classic_ui::HILIGHT);
    classic_ui::text(x+24,y+132,"Setup progress",classic_ui::ACTIVEBLUE);
    classic_ui::fill({x+20,y+152,w-40,132},classic_ui::FACE); classic_ui::frame3d({x+20,y+152,w-40,132},false);
    classic_ui::text(x+34,y+172,"Target:",classic_ui::BLACK); classic_ui::text(x+98,y+172,"AHCI SATA system disk",classic_ui::BLACK);
    classic_ui::text(x+34,y+192,"Source:",classic_ui::BLACK); classic_ui::text(x+98,y+192,"zeroNT factory image",classic_ui::BLACK);
    classic_ui::text(x+34,y+218,status?status:"Preparing installation...",error?classic_ui::Color{160,0,0}:classic_ui::BLACK);
    const int bx=x+34,by=y+240,bw=w-68,bh=22;
    classic_ui::fill({bx,by,bw,bh},classic_ui::WHITE); classic_ui::frame3d({bx,by,bw,bh},true);
    uint32_t pct=total?(uint32_t)((copied*100ULL)/total):0;if(pct>100)pct=100;
    int usable=((bw-6)*(int)pct)/100; classic_ui::Color green={0,160,0}, red={160,0,0};
    for(int p=0;p+7<=usable;p+=10)classic_ui::fill({bx+3+p,by+3,7,bh-6},error?red:green);
    char pctText[5]{}; pctText[0]=(char)('0'+(pct/100)%10); pctText[1]=(char)('0'+(pct/10)%10); pctText[2]=(char)('0'+pct%10); pctText[3]='%';
    const char* shown=pctText; if(pct<100)shown=pctText+1; if(pct<10)shown=pctText+2;
    classic_ui::text(x+34,y+269,shown,classic_ui::BLACK);
    classic_ui::hline(x+8,x+w-9,y+h-58,classic_ui::SHADOW); classic_ui::hline(x+8,x+w-9,y+h-57,classic_ui::HILIGHT);
    classic_ui::text(x+20,y+h-36,error?"Setup stopped.":"Setup will continue automatically when this step is complete.",classic_ui::BLACK);
    classic_ui::button({x+w-104,y+h-44,80,26},"Cancel",false,true);
    classic_ui::present();
}

static uint32_t gInstallerLastPercent = 0xFFFFFFFFu;
static bool gInstallerFrameDrawn = false;
static void installer_progress_draw(uint64_t copied,uint64_t total,void*) {
    uint32_t pct = total ? static_cast<uint32_t>((copied*100ULL)/total) : 0;
    if (pct > 100) pct = 100;
    // Draw the complete classic dialog once. During the copy, update only the
    // progress control and percentage so QEMU never shows half-painted windows.
    if (!gInstallerFrameDrawn) {
        draw_installer_screen(copied,total,"COPYING SYSTEM FILES TO DISK...");
        gInstallerFrameDrawn = true;
        gInstallerLastPercent = pct;
        return;
    }
    if (pct == gInstallerLastPercent && copied != total) return;
    gInstallerLastPercent = pct;
    const int w=(gBoot->width>820?780:(int)gBoot->width-30), h=390;
    const int x=((int)gBoot->width-w)/2, y=((int)gBoot->height-h)/2;
    const int bx=x+34, by=y+240, bw=w-68, bh=22;
    classic_ui::fill({bx,by,bw,bh},classic_ui::WHITE); classic_ui::frame3d({bx,by,bw,bh},true);
    const int usable=((bw-6)*(int)pct)/100; classic_ui::Color green={0,160,0};
    for(int p=0;p+7<=usable;p+=10)classic_ui::fill({bx+3+p,by+3,7,bh-6},green);
    classic_ui::fill({x+34,y+266,54,14},classic_ui::FACE);
    char pctText[5]{}; pctText[0]=(char)('0'+(pct/100)%10); pctText[1]=(char)('0'+(pct/10)%10); pctText[2]=(char)('0'+pct%10); pctText[3]='%';
    const char* shown=pctText; if(pct<100)shown=pctText+1; if(pct<10)shown=pctText+2; classic_ui::text(x+34,y+269,shown,classic_ui::BLACK);
    // Only publish the pixels that changed.  The disk copy, FAT32 writes,
    // network/audio polling and all other processing continue exactly as
    // before; only the visible update is made atomic and small.
    classic_ui::present_rect({bx,by,bw,48});
}


static uint8_t installer_wait_scancode() {
    for (;;) {
        xhci_poll();
        uint8_t usbSc=0;
        if (usb_keyboard_pop(usbSc)) return usbSc;
        const uint8_t status=inb(0x64);
        if(status&0x01u){
            const uint8_t data=inb(0x60);
            if((status&0x20u)==0)return data;
        }
        __asm__ __volatile__("pause");
    }
}

static void draw_installer_disk_picker(BlockDevice* disks,uint32_t count,uint32_t selected,const char* message) {
    classic_ui::fill({0,0,(int)gBoot->width,(int)gBoot->height},classic_ui::DESKTOP);
    const int w=(gBoot->width>820?780:(int)gBoot->width-30), h=430;
    const int x=((int)gBoot->width-w)/2, y=((int)gBoot->height-h)/2;
    classic_ui::window_frame({x,y,w,h},"zeroNT Setup - Select Installation Disk",true,false,false,false);
    classic_ui::fill({x+4,y+24,w-8,82},classic_ui::WHITE);
    classic_ui::text(x+24,y+42,"Where do you want to install zeroNT?",classic_ui::ACTIVEBLUE,2);
    classic_ui::text(x+24,y+72,"Use UP/DOWN to choose a disk, then press ENTER.",classic_ui::BLACK);
    classic_ui::hline(x+4,x+w-5,y+105,classic_ui::SHADOW); classic_ui::hline(x+4,x+w-5,y+106,classic_ui::HILIGHT);
    int rowY=y+132;
    for(uint32_t i=0;i<count&&i<6;++i){
        const bool sel=i==selected;
        classic_ui::fill({x+24,rowY-5,w-48,38},sel?classic_ui::ACTIVEBLUE:classic_ui::WHITE);
        classic_ui::frame3d({x+24,rowY-5,w-48,38},!sel);
        classic_ui::text(x+38,rowY,disks[i].name?disks[i].name:"Storage device",sel?classic_ui::WHITE:classic_ui::BLACK);
        classic_ui::text(x+w-235,rowY,"SIZE:",sel?classic_ui::WHITE:classic_ui::BLACK);
        draw_u64(x+w-185,rowY,(disks[i].sectorCount*uint64_t(disks[i].sectorSize))/(1024ULL*1024ULL),sel?pack_rgb(255,255,255):pack_rgb(0,0,0),1);
        classic_ui::text(x+w-125,rowY,"MiB",sel?classic_ui::WHITE:classic_ui::BLACK);
        rowY+=46;
    }
    if(message&&*message)classic_ui::text(x+24,y+h-92,message,{128,0,0});
    classic_ui::hline(x+8,x+w-9,y+h-62,classic_ui::SHADOW); classic_ui::hline(x+8,x+w-9,y+h-61,classic_ui::HILIGHT);
    classic_ui::text(x+20,y+h-40,"ENTER = Continue     ESC = Cancel",classic_ui::BLACK);
    classic_ui::present();
}

static int installer_choose_target(BlockDevice* disks,uint32_t count) {
    if(!disks||count==0)return -1;
    uint32_t selected=0;
    for(;;){
        draw_installer_disk_picker(disks,count,selected,nullptr);
        const uint8_t sc=installer_wait_scancode();
        if(sc&0x80u)continue;
        if(sc==0x48u){selected=selected?selected-1:count-1;continue;}
        if(sc==0x50u){selected=(selected+1)%count;continue;}
        if(sc==0x1Cu)return static_cast<int>(selected);
        if(sc==0x01u)return -1;
    }
}

static bool installer_confirm_format(const BlockDevice& disk) {
    for(;;){
        classic_ui::fill({0,0,(int)gBoot->width,(int)gBoot->height},classic_ui::DESKTOP);
        const int w=(gBoot->width>760?720:(int)gBoot->width-30),h=320;
        const int x=((int)gBoot->width-w)/2,y=((int)gBoot->height-h)/2;
        classic_ui::window_frame({x,y,w,h},"zeroNT Setup - Confirm Format",true,false,false,false);
        classic_ui::fill({x+4,y+24,w-8,80},classic_ui::WHITE);
        classic_ui::text(x+24,y+42,"WARNING",{160,0,0},2);
        classic_ui::text(x+24,y+72,"The selected disk will be repartitioned and formatted.",classic_ui::BLACK);
        classic_ui::text(x+30,y+130,"Disk:",classic_ui::BLACK);
        classic_ui::text(x+92,y+130,disk.name?disk.name:"Storage device",classic_ui::BLACK);
        classic_ui::text(x+30,y+154,"Capacity:",classic_ui::BLACK);
        draw_u64(x+112,y+154,(disk.sectorCount*uint64_t(disk.sectorSize))/(1024ULL*1024ULL),pack_rgb(0,0,0),1);
        classic_ui::text(x+176,y+154,"MiB",classic_ui::BLACK);
        classic_ui::text(x+30,y+198,"ALL DATA ON THIS DISK MAY BE LOST.",{160,0,0});
        classic_ui::hline(x+8,x+w-9,y+h-62,classic_ui::SHADOW);classic_ui::hline(x+8,x+w-9,y+h-61,classic_ui::HILIGHT);
        classic_ui::text(x+20,y+h-40,"ENTER = FORMAT AND INSTALL     ESC = Go Back",classic_ui::BLACK);
        classic_ui::present();
        const uint8_t sc=installer_wait_scancode();
        if(sc&0x80u)continue;
        if(sc==0x1Cu)return true;
        if(sc==0x01u)return false;
    }
}

[[noreturn]] static void installer_reboot_after_success() {
    draw_installer_screen(1,1,"INSTALLATION COMPLETE - RESTARTING...");
    for(volatile uint64_t spin=0;spin<120000000ULL;++spin)__asm__ __volatile__("pause");
    acpi_reboot();
}

extern "C" __attribute__((sysv_abi)) void kernel_entry(ZeroNTBootInfo* info) {
    gBoot = info;
    classic_ui::init(info);
    interrupts_disable();
    if (!gBoot || gBoot->magic != ZERONT_BOOTINFO_MAGIC || !gBoot->framebuffer) {
        for (;;) cpu_halt();
    }

    // Show a framebuffer message immediately after the loader handoff. If later
    // initialization stalls, the last visible OK line identifies the stage.
    early_boot_begin();
    early_boot_stage("KERNEL ENTRY + DEDICATED STACK");

    quality_init();
    event_log_init(nullptr);
    security_init();
    early_boot_stage("QUALITY + SECURITY CORE");
    gdt_init();
    idt_init();
    early_boot_stage("GDT + IDT");
    pmm_init(gBoot);
    early_boot_stage("PHYSICAL MEMORY MANAGER");

    // Build zeroNT-owned page tables, preserve a 4 GiB identity map, then
    // switch CR3 away from the UEFI firmware page tables.
    if (!vmm_init(gBoot)) {
        draw_text(22, gEarlyBootY, "FAIL VMM INIT", pack_rgb(255,80,80), 1);
        for (;;) cpu_halt();
    }
    early_boot_stage("VIRTUAL MEMORY + CR3");

    // Map a real high-half kernel heap and prove byte-granularity allocation.
    if (!heap_init()) {
        draw_text(22, gEarlyBootY, "FAIL HEAP INIT", pack_rgb(255,80,80), 1);
        for (;;) cpu_halt();
    }
    early_boot_stage("KERNEL HEAP");
    // From this point on, UI drawing is off-screen.  The visible framebuffer
    // is touched only by present()/present_rect(), preventing the user from
    // seeing intermediate draw operations while all kernel work keeps running.
    (void)classic_ui::enable_backbuffer();
    void* heapTest = kmalloc(128);
    if (heapTest) {
        auto* p = static_cast<uint8_t*>(heapTest);
        for (uint32_t i=0;i<128;++i) p[i]=static_cast<uint8_t>(i);
    }

    // v24 kernel GUI service is available to ring-3 applications through syscalls.
    gui_init(gBoot);
    early_boot_stage("GUI CORE");
    // Hardware discovery is intentionally staged.  ACPI comes first so MADT/HPET
    // information is available before PCI capability and APIC diagnostics.
    bool acpiReady = gBoot->acpiRsdp ? acpi_init(gBoot->acpiRsdp) : false;
    AcpiStats acpiInfo = acpi_stats();
    early_boot_stage(acpiReady ? "ACPI TABLE PROBE OK" : "ACPI TABLE PROBE SKIPPED");

    early_boot_stage("PCI ENUMERATION START");
    driver_manager_init_pci();
    early_boot_stage("PCI ENUMERATION OK");

    const bool displayReady=display_init(gBoot);
    early_boot_stage(displayReady ? "DISPLAY DRIVER READY" : "DISPLAY DRIVER FALLBACK");

    const bool platformReady=platform_hardware_init();
    PlatformHardwareStats platformInfo=platform_hardware_stats();
    early_boot_stage(platformReady ? "APIC/HPET/PCI CAPS READY" : "APIC/HPET OPTIONAL");

    nvme_probe();
    NvmeProbeStats nvmeStats = nvme_probe_stats();
    early_boot_stage(nvmeStats.found ? "NVME PCI DEVICE FOUND" : "NVME PCI DEVICE NOT FOUND");

    usb_probe();
    UsbProbeStats usbStats = usb_probe_stats();
    early_boot_stage(usbStats.controllers ? "USB PCI PROBE OK" : "USB PCI DEVICE NOT FOUND");
    bool xhciReady = usbStats.xhciFound ? xhci_init() : false;
    early_boot_stage(xhciReady ? "XHCI CORE READY" : (usbStats.xhciFound ? "XHCI CORE INIT FAILED" : "XHCI NOT PRESENT"));

    driver_manager_update_optional(nvmeStats.found, xhciReady, acpiReady);
    DriverManagerStats driverStats = driver_manager_stats();
    early_boot_stage("DRIVER MANAGER READY");
    (void)driverStats; (void)nvmeStats; (void)usbStats; (void)acpiInfo; (void)platformInfo;

    // v26: real QEMU-friendly audio + network devices.
    bool audioReady = audio_init();
    if (audioReady) audio_play_system_sound(SystemSound::Startup);
    bool networkReady = network_init();
    early_boot_stage("AUDIO + NETWORK PROBE");
    (void)networkReady;

    // Production boot: initialize user mode, but do not deliberately fault or run
    // demo smoke tests on every normal boot.
    early_boot_stage("USERMODE + SYSCALL CORE");
    bool ring3Ok = usermode_init();
    UserModeStats userStats = usermode_stats();
    (void)ring3Ok; (void)userStats;

    // v32: the boot media always provides a pristine FAT32 system template in RAM.
    // A separate AHCI disk is the persistent install target.  On a blank target,
    // zeroNT itself performs the installation before the account setup starts.
    BlockDevice installSource{};
    const bool installSourceReady = gBoot->rootfsBase && gBoot->rootfsSize && gBoot->rootfsSectorSize &&
        ramdisk_init(installSource, reinterpret_cast<void*>(static_cast<uintptr_t>(gBoot->rootfsBase)),
                     gBoot->rootfsSize, gBoot->rootfsSectorSize);

    BlockDevice rootDevice{};
    Fat32Volume fat{};
    early_boot_stage("STORAGE INITIALIZATION");
    BlockDevice installCandidates[3]{};
    uint32_t installCandidateCount=0;
    BlockDevice ahciDevice{},nvmeDevice{};
    bool usingAhci = ahci_init_first(ahciDevice);
    bool usingNvme = nvme_init_first(nvmeDevice);
    [[maybe_unused]] bool usingUsbStorage = false;
    if(usingAhci)installCandidates[installCandidateCount++]=ahciDevice;
    if(usingNvme&&installCandidateCount<3)installCandidates[installCandidateCount++]=nvmeDevice;
    BlockDevice* usbStorage = usb_mass_storage_device();
    if(usbStorage&&installCandidateCount<3){installCandidates[installCandidateCount++]=*usbStorage;usingUsbStorage=true;}
    [[maybe_unused]] AhciStats ahci = ahci_stats();
    [[maybe_unused]] NvmeStats nvme = nvme_stats();
    bool fatMounted = false;
    bool installedThisBoot = false;
    char rootListing[160]{};
    char readmePreview[128]{};
    [[maybe_unused]] bool writeTestOk = false;

    if (installCandidateCount && installSourceReady) {
        int chosen=-1;
        for(uint32_t i=0;i<installCandidateCount;++i){
            installer_init(&installSource,&installCandidates[i]);
            const InstallerTargetState state=installer_probe_target();
            if(state==InstallerTargetState::Installed||state==InstallerTargetState::ExistingZeroNT){chosen=static_cast<int>(i);break;}
        }
        if(chosen<0){
            chosen=installer_choose_target(installCandidates,installCandidateCount);
            if(chosen<0){draw_installer_screen(0,1,"INSTALLATION CANCELLED",true);for(;;)cpu_halt();}
            if(!installer_confirm_format(installCandidates[chosen])){
                draw_installer_screen(0,1,"INSTALLATION CANCELLED - NO DISK WAS MODIFIED",true);for(;;)cpu_halt();
            }
        }
        rootDevice=installCandidates[chosen];
        usingAhci=usingAhci&&rootDevice.context==ahciDevice.context;
        usingNvme=usingNvme&&rootDevice.context==nvmeDevice.context;
        usingUsbStorage=usbStorage&&rootDevice.context==usbStorage->context;
        installer_init(&installSource, &rootDevice);
        if (installer_needs_install()) {
            gInstallerLastPercent = 0xFFFFFFFFu;
            gInstallerFrameDrawn = false;
            draw_installer_screen(0, installSource.sectorCount, "PREPARING TARGET DISK...");
            installedThisBoot = installer_install(installer_progress_draw, nullptr);
            if (!installedThisBoot) {
                draw_installer_screen(installer_stats().copiedSectors, installSource.sectorCount,
                                      "INSTALL FAILED - CHECK TARGET STORAGE", true);
                for (;;) cpu_halt();
            }
            installer_reboot_after_success();
        }
        fatMounted = installer_mount_target(fat);
        if (!fatMounted) {
            draw_installer_screen(installSource.sectorCount, installSource.sectorCount,
                                  "INSTALLED FILES COULD NOT BE MOUNTED", true);
            for (;;) cpu_halt();
        }
    } else if (installSourceReady) {
        // Developer fallback only: boot the template read/write in RAM when no
        // installable AHCI target exists.  This session is not persistent.
        rootDevice = installSource;
        fatMounted = fat32_mount(&rootDevice, fat);
    }

    if (fatMounted) {
        fat32_list_directory(fat, "/", root_list_callback, rootListing);
        size_t got = 0;
        if (fat32_read_file(fat, "/README.TXT", readmePreview, sizeof(readmePreview)-1, got)) {
            if (got >= sizeof(readmePreview)) got = sizeof(readmePreview)-1;
            readmePreview[got] = 0;
            sanitize_preview(readmePreview, sizeof(readmePreview));
        }
    }

    // Recover interrupted SafeFS work, validate the now-installed target, then
    // initialize the user/session layer on that persistent filesystem.
    if (fatMounted) {
        safe_fs_init(&fat);
        safe_fs_recover_pending();
        event_log_init(&fat);
        display_attach_filesystem(&fat);
        (void)display_apply_saved_mode();
        event_log_write(EventLevel::Info,"BOOT",installedThisBoot?"ZERONT V33.0 FIRST BOOT AFTER INSTALL":"ZERONT V33.0 BOOT");
    }
    bool userSystemReady = fatMounted && user_system_init(fat);
    if (!userSystemReady) {
        draw_installer_screen(installSource.sectorCount, installSource.sectorCount,
                              "USER DATABASE INITIALIZATION FAILED", true);
        for (;;) cpu_halt();
    }
    bool systemAppsReady = system_apps_init(gBoot, &fat);
    if (!systemAppsReady) {
        draw_installer_screen(installSource.sectorCount, installSource.sectorCount,
                              "DESKTOP COMPONENT INITIALIZATION FAILED", true);
        for (;;) cpu_halt();
    }
    [[maybe_unused]] ExecutableStats appStats{};
    [[maybe_unused]] ExecutableStats guiAppStats{};

    // On a normal already-installed boot, finish the splash smoothly to 100%.
    // First-install boots keep the Installer screen in front instead of painting
    // the splash back over it.
    if (!installedThisBoot) early_boot_finish();

    // Stable runtime startup: keep only kernel-main during Setup/Login.
    // The old release created two busy demo workers here; on some QEMU builds
    // they could preempt the setup path immediately after installation.
    process_init();
    const uint64_t kernelPid = process_create("kernel");
    scheduler_init(kernelPid);
    process_attach_thread(kernelPid, scheduler_current_tid());

    pic_init();
    pit_init(100);
    ps2_init();
    scheduler_start();

    // Keep one PMM page reserved as a visible allocator test.
    [[maybe_unused]] void* testPage = pmm_alloc_page();

    char typed[256]{};
    uint32_t typedLen=0;
    bool shift=false;
    bool ctrl=false;
    bool alt=false;
    int mouseX = static_cast<int>(gBoot->width/2);
    int mouseY = static_cast<int>(gBoot->height/2);
    uint8_t mouseButtons=0;
    uint8_t mousePacket[4]{};
    uint32_t mouseIndex=0;
    uint64_t lastDrawTick = static_cast<uint64_t>(-1);
    bool dirty=true;
    bool mouseMotionPending=false;

    UserSystemStats accountAtBoot = user_system_stats();
    AuthStage authStage = accountAtBoot.setupRequired ? AuthStage::SetupName : AuthStage::LoginName;
    char authName[32]{};
    char authPassword[48]{};
    char authConfirm[48]{};
    char authMessage[80]{};
    if(installedThisBoot) text_set(authMessage,sizeof(authMessage),"SYSTEM INSTALLED. CREATE YOUR ACCOUNT.");
    bool sessionAppsStarted=false;
    bool betaStarted=false;

    auto process_scancode = [&](uint8_t sc) {
        dirty=true;
        if (sc == 0x2A || sc == 0x36) { shift=true; system_apps_set_modifiers(shift,ctrl,alt); return; }
        if (sc == 0xAA || sc == 0xB6) { shift=false; system_apps_set_modifiers(shift,ctrl,alt); return; }
        if (sc == 0x1D) { ctrl=true; system_apps_set_modifiers(shift,ctrl,alt); return; }
        if (sc == 0x9D) { ctrl=false; system_apps_set_modifiers(shift,ctrl,alt); return; }
        if (sc == 0x38) { alt=true; system_apps_set_modifiers(shift,ctrl,alt); return; }
        if (sc == 0xB8) { alt=false; system_apps_set_modifiers(shift,ctrl,alt); return; }
        if (sc & 0x80) return;

        if (!user_is_logged_in()) {
            char* target=(authStage==AuthStage::SetupName||authStage==AuthStage::LoginName)?authName:
                         (authStage==AuthStage::SetupConfirm?authConfirm:authPassword);
            uint32_t cap=(target==authName)?sizeof(authName):sizeof(authPassword);
            if (sc==0x0E) { text_backspace(target); return; }
            if (sc==0x1C) {
                if (authStage==AuthStage::SetupName) {
                    if (!user_validate_display_name(authName)) text_set(authMessage,sizeof(authMessage),"INVALID NAME. 1-31 ASCII CHARS; NO PATH SYMBOLS.");
                    else { authStage=AuthStage::SetupPassword; text_clear(authMessage,sizeof(authMessage)); }
                } else if (authStage==AuthStage::SetupPassword) {
                    if (!user_validate_password(authPassword)) text_set(authMessage,sizeof(authMessage),"PASSWORD: 8-47 CHARS, INCLUDE A LETTER AND NUMBER.");
                    else { authStage=AuthStage::SetupConfirm; text_clear(authConfirm,sizeof(authConfirm)); text_clear(authMessage,sizeof(authMessage)); }
                } else if (authStage==AuthStage::SetupConfirm) {
                    if (!text_same(authPassword,authConfirm)) { text_set(authMessage,sizeof(authMessage),"PASSWORDS DO NOT MATCH."); text_clear(authConfirm,sizeof(authConfirm)); }
                    else if (user_setup_create_admin(fat,authName,authPassword,timer_ticks()^uint64_t(mouseX<<16)^uint64_t(mouseY))) {
                        authStage=AuthStage::Session; text_set(authMessage,sizeof(authMessage),"ACCOUNT CREATED"); text_clear(authPassword,sizeof(authPassword)); text_clear(authConfirm,sizeof(authConfirm));
                    } else text_set(authMessage,sizeof(authMessage),"COULD NOT CREATE ACCOUNT OR PROFILE.");
                } else if (authStage==AuthStage::LoginName) {
                    if (!user_validate_display_name(authName)) text_set(authMessage,sizeof(authMessage),"ENTER YOUR USER NAME.");
                    else { authStage=AuthStage::LoginPassword; text_clear(authPassword,sizeof(authPassword)); text_clear(authMessage,sizeof(authMessage)); }
                } else if (authStage==AuthStage::LoginPassword) {
                    if (user_login(fat,authName,authPassword)) { authStage=AuthStage::Session; text_clear(authPassword,sizeof(authPassword)); text_clear(authMessage,sizeof(authMessage)); }
                    else { text_set(authMessage,sizeof(authMessage),"USER NAME OR PASSWORD IS INCORRECT."); text_clear(authPassword,sizeof(authPassword)); authStage=AuthStage::LoginName; }
                }
                return;
            }
            char c=scan_to_ascii(sc,shift);
            if(c>=32&&c<=126) text_push(target,cap,c);
            return;
        }

        char c=scan_to_ascii(sc,shift);
        const uint32_t guiMods=(shift?1u:0u)|(ctrl?2u:0u)|(alt?4u:0u);
        if(!gui_post_key(sc,static_cast<uint32_t>(static_cast<unsigned char>(c)),guiMods)) system_apps_key(sc,c);
        if (sc == 0x0E) { if (typedLen) typed[--typedLen]=0; return; }
        if (c >= 32 && c <= 126 && typedLen+1 < sizeof(typed)) { typed[typedLen++]=c; typed[typedLen]=0; }
    };

    auto dispatch_mouse = [&](int dx,int dyScreen,int wheel,uint8_t buttons) {
        const int oldMouseX=mouseX, oldMouseY=mouseY;
        mouseButtons=buttons;
        mouseX+=dx; mouseY+=dyScreen;
        if(mouseX<0)mouseX=0; if(mouseY<0)mouseY=0;
        if(mouseX>=static_cast<int>(gBoot->width))mouseX=static_cast<int>(gBoot->width)-1;
        if(mouseY>=static_cast<int>(gBoot->height))mouseY=static_cast<int>(gBoot->height)-1;
        const bool guiConsumed=gui_post_mouse(mouseX,mouseY,mouseButtons);
        if(!guiConsumed)system_apps_mouse(mouseX,mouseY,mouseButtons);
        if(wheel&&!gui_has_active_window())system_apps_wheel(mouseX,mouseY,wheel);
        if(user_is_logged_in()){
            if(system_apps_dirty())dirty=true;
            else if(oldMouseX!=mouseX||oldMouseY!=mouseY){ classic_ui::move_mouse(oldMouseX,oldMouseY,mouseX,mouseY); mouseMotionPending=true; }
        }
    };

    // Do not leave the completed progress page on screen while the rest of the
    // kernel becomes interactive. Switch to Setup/Login immediately.
    // Setup/Login is event-driven: draw it once, then redraw only when input or
    // stage/message data actually changes. Repainting the whole framebuffer on
    // the 100 Hz PIT clock caused visible flicker on QEMU/OVMF.
    draw_auth_screen(authStage,authName,authPassword,authMessage);
    dirty=false;
    lastDrawTick=timer_ticks();
    interrupts_enable();

    for (;;) {
        // Poll xHCI before draining input queues so USB HID feels as immediate as PS/2.
        xhci_poll();
        uint8_t sc=0;
        while (keyboard_pop(sc)) process_scancode(sc);
        while (usb_keyboard_pop(sc)) process_scancode(sc);


        uint8_t mb=0;
        const uint8_t mousePacketBytes = mouse_packet_size();
        while (mouse_pop(mb)) {
            // PS/2 byte 0 always has bit 3 set.  This recovers cleanly if a byte
            // is lost instead of letting one bad byte shift every later packet.
            if (mouseIndex==0 && (mb & 0x08)==0) continue;
            mousePacket[mouseIndex++]=mb;
            if (mouseIndex==mousePacketBytes) {
                mouseIndex=0;
                const uint8_t flags=mousePacket[0];
                mouseButtons=static_cast<uint8_t>(flags & 0x07);

                // X/Y are signed *9-bit* PS/2 values.  Casting byte 1/2 to
                // int8_t makes fast positive motion above +127 wrap negative,
                // which feels like random direction changes.  Decode using the
                // sign bits in byte 0 and ignore overflowed packets.
                const bool overflow=(flags & 0xC0)!=0;
                int dx=static_cast<int>(mousePacket[1]);
                int dy=static_cast<int>(mousePacket[2]);
                if(flags & 0x10) dx-=256;
                if(flags & 0x20) dy-=256;

                int wheel=0;
                if (mousePacketBytes == 4) {
                    wheel = static_cast<int8_t>(mousePacket[3]);
                    if ((mousePacket[3] & 0xF0) != 0 && (mousePacket[3] & 0xF0) != 0xF0) {
                        int8_t nib = static_cast<int8_t>(mousePacket[3] & 0x0F);
                        if (nib & 0x08) nib = static_cast<int8_t>(nib | 0xF0);
                        wheel = nib;
                    }
                }
                if(!overflow) dispatch_mouse(dx,-dy,wheel,mouseButtons);
                else dispatch_mouse(0,0,wheel,mouseButtons);
            }
        }

        UsbMouseEvent usbMouse{};
        while (usb_mouse_pop(usbMouse)) dispatch_mouse(usbMouse.dx,usbMouse.dy,usbMouse.wheel,usbMouse.buttons);

        if (user_is_logged_in() && system_apps_take_logout_request()) {
            system_apps_on_logout();
            user_logout();
            authStage=AuthStage::LoginName;
            text_clear(authName,sizeof(authName));
            text_clear(authPassword,sizeof(authPassword));
            text_set(authMessage,sizeof(authMessage),"LOGGED OUT");
            sessionAppsStarted=false;
            dirty=true;
        }

        if (user_is_logged_in() && !sessionAppsStarted) {
            // Heavy health checks are intentionally deferred until the interactive
            // setup/login transition has completed.
            if (!betaStarted) { beta_init(&fat); betaStarted=true; }
            system_apps_on_login();
            sessionAppsStarted=true; dirty=true;
        }
        if (!user_is_logged_in()) sessionAppsStarted=false;

        executable_poll();
        if(gui_dirty())dirty=true;
        audio_poll();
        network_poll();
        platform_hardware_poll();
        const uint64_t ticks=timer_ticks();
        driver_manager_poll(ticks);
        if(fatMounted){beta_poll(ticks);event_log_poll(ticks);}
        // Pure pointer motion is immediate through the tiny hardware-cursor
        // path above.  Refresh the full classic shell at most 10 Hz while the
        // mouse is moving so hover/pressed visuals still track naturally, and
        // once per second while idle for the clock/status areas.
        if(user_is_logged_in()&&mouseMotionPending&&ticks-lastDrawTick>=10)dirty=true;
        if(user_is_logged_in()&&ticks/6000!=lastDrawTick/6000)dirty=true;
        if (dirty) {
            if (user_is_logged_in()) {
                system_apps_render(mouseX,mouseY);
            } else {
                draw_auth_screen(authStage,authName,(authStage==AuthStage::SetupConfirm?authConfirm:authPassword),authMessage);
            }
            dirty=false;
            mouseMotionPending=false;
            lastDrawTick=ticks;
        }

        cpu_halt();
    }
}
