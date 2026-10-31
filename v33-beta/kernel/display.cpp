#include "include/display.hpp"
#include "include/pci.hpp"
#include "include/arch.hpp"
#include "include/paging.hpp"
#include "include/classic_ui.hpp"
#include "include/gui.hpp"
#include "include/fat32.hpp"
#include "include/event_log.hpp"
#include <stddef.h>

namespace {
constexpr uint64_t FOUR_GIB = 0x100000000ULL;
constexpr uint64_t PAGE_SIZE = 4096ULL;

// VMware SVGA II register interface.
constexpr uint32_t SVGA_ID_2 = 0x90000002u;
constexpr uint32_t SVGA_REG_ID = 0;
constexpr uint32_t SVGA_REG_ENABLE = 1;
constexpr uint32_t SVGA_REG_WIDTH = 2;
constexpr uint32_t SVGA_REG_HEIGHT = 3;
constexpr uint32_t SVGA_REG_MAX_WIDTH = 4;
constexpr uint32_t SVGA_REG_MAX_HEIGHT = 5;
constexpr uint32_t SVGA_REG_BITS_PER_PIXEL = 7;
constexpr uint32_t SVGA_REG_RED_MASK = 9;
constexpr uint32_t SVGA_REG_BLUE_MASK = 11;
constexpr uint32_t SVGA_REG_BYTES_PER_LINE = 12;
constexpr uint32_t SVGA_REG_FB_START = 13;
constexpr uint32_t SVGA_REG_FB_SIZE = 16;
constexpr uint32_t SVGA_REG_SYNC = 21;
constexpr uint32_t SVGA_REG_BUSY = 22;
constexpr uint32_t SVGA_ENABLE = 1;

// Bochs/QEMU DISPI interface.
constexpr uint16_t BOCHS_INDEX = 0x01CE;
constexpr uint16_t BOCHS_DATA  = 0x01CF;
constexpr uint16_t BGA_REG_ID = 0;
constexpr uint16_t BGA_REG_XRES = 1;
constexpr uint16_t BGA_REG_YRES = 2;
constexpr uint16_t BGA_REG_BPP = 3;
constexpr uint16_t BGA_REG_ENABLE = 4;
constexpr uint16_t BGA_REG_VIRT_WIDTH = 6;
constexpr uint16_t BGA_REG_X_OFFSET = 8;
constexpr uint16_t BGA_REG_Y_OFFSET = 9;
constexpr uint16_t BGA_ENABLED = 0x01;
constexpr uint16_t BGA_GETCAPS = 0x02;
constexpr uint16_t BGA_LFB_ENABLED = 0x40;

static ZeroNTBootInfo* gBoot = nullptr;
static Fat32Volume* gFs = nullptr;
static DisplayStats gStats{};
static uint16_t gVmwareIo = 0;
static uint64_t gBochsFb = 0;
static uint64_t gBochsFbSize = 0;
static bool gVmwareFound = false;
static bool gBochsFound = false;

static void vmw_write(uint32_t reg, uint32_t value) {
    outl(gVmwareIo + 0, reg);
    outl(gVmwareIo + 1, value);
}
static uint32_t vmw_read(uint32_t reg) {
    outl(gVmwareIo + 0, reg);
    return inl(gVmwareIo + 1);
}
static void bga_write(uint16_t reg, uint16_t value) { outw(BOCHS_INDEX, reg); outw(BOCHS_DATA, value); }
static uint16_t bga_read(uint16_t reg) { outw(BOCHS_INDEX, reg); return inw(BOCHS_DATA); }

static bool map_mmio_if_needed(uint64_t base, uint64_t bytes) {
    if (!base || !bytes) return false;
    const uint64_t end = base + bytes;
    if (end < base) return false;
    if (end <= FOUR_GIB) return true; // v33 already identity maps the first 4 GiB.
    uint64_t p = base & ~(PAGE_SIZE - 1ULL);
    const uint64_t last = (end + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    for (; p < last; p += PAGE_SIZE) {
        if (!vmm_map_page(p, p, PagePresent | PageWritable | PageWriteThrough | PageCacheDisable)) return false;
    }
    return true;
}

static void detect_pci(const PciDeviceInfo& d, void*) {
    if (d.vendorId == 0x15AD && (d.deviceId == 0x0405 || d.deviceId == 0x0710)) {
        const uint32_t bar0 = d.bar[0];
        if ((bar0 & 1u) != 0) {
            pci_enable_io_busmaster(d);
            gVmwareIo = static_cast<uint16_t>(bar0 & ~3u);
            gVmwareFound = gVmwareIo != 0;
        }
    }
    if ((d.vendorId == 0x1234 && d.deviceId == 0x1111) ||
        (d.vendorId == 0x1AF4 && d.classCode == 0x03)) {
        const uint32_t bar0 = d.bar[0];
        if ((bar0 & 1u) == 0 && (bar0 & ~0xFu) != 0) {
            pci_enable_mmio_busmaster(d);
            gBochsFb = static_cast<uint64_t>(bar0 & ~0xFu);
            gBochsFound = true;
        }
    }
}

static bool vmware_probe() {
    if (!gVmwareFound || !gVmwareIo) return false;
    vmw_write(SVGA_REG_ID, SVGA_ID_2);
    if (vmw_read(SVGA_REG_ID) != SVGA_ID_2) return false;
    uint32_t mw = vmw_read(SVGA_REG_MAX_WIDTH);
    uint32_t mh = vmw_read(SVGA_REG_MAX_HEIGHT);
    if (mw < 640 || mw > 8192) mw = 2560;
    if (mh < 480 || mh > 8192) mh = 1600;
    gStats.backend = DisplayBackend::VMwareSvgaII;
    gStats.maxWidth = mw;
    gStats.maxHeight = mh;
    gStats.bitsPerPixel = vmw_read(SVGA_REG_BITS_PER_PIXEL);
    if (!gStats.bitsPerPixel) gStats.bitsPerPixel = 32;
    return true;
}

static bool bochs_probe() {
    if (!gBochsFound || !gBochsFb) return false;
    const uint16_t id = bga_read(BGA_REG_ID);
    if (id < 0xB0C0u || id > 0xB0C5u) return false;
    bga_write(BGA_REG_ENABLE, BGA_GETCAPS);
    uint32_t mw = bga_read(BGA_REG_XRES);
    uint32_t mh = bga_read(BGA_REG_YRES);
    uint32_t bpp = bga_read(BGA_REG_BPP);
    bga_write(BGA_REG_ENABLE, 0);
    if (mw < 640 || mw > 8192) mw = 2560;
    if (mh < 480 || mh > 8192) mh = 1600;
    if (bpp < 32) bpp = 32;
    gStats.backend = DisplayBackend::BochsVbe;
    gStats.maxWidth = mw;
    gStats.maxHeight = mh;
    gStats.bitsPerPixel = bpp;
    // QEMU stdvga BAR0 is normally the complete linear framebuffer aperture.
    gBochsFbSize = static_cast<uint64_t>(mw) * static_cast<uint64_t>(mh) * 4ULL;
    return true;
}

static uint32_t vmware_pixel_format() {
    const uint32_t red = vmw_read(SVGA_REG_RED_MASK);
    const uint32_t blue = vmw_read(SVGA_REG_BLUE_MASK);
    // GOP format 0 means R is in the low byte; format 1 means B is in the low byte.
    if (red == 0x000000FFu && blue == 0x00FF0000u) return 0;
    return 1;
}

static bool vmware_set_mode(uint32_t width, uint32_t height) {
    vmw_write(SVGA_REG_ENABLE, 0);
    vmw_write(SVGA_REG_WIDTH, width);
    vmw_write(SVGA_REG_HEIGHT, height);
    vmw_write(SVGA_REG_BITS_PER_PIXEL, 32);
    vmw_write(SVGA_REG_ENABLE, SVGA_ENABLE);
    const uint32_t actualW = vmw_read(SVGA_REG_WIDTH);
    const uint32_t actualH = vmw_read(SVGA_REG_HEIGHT);
    const uint32_t pitch = vmw_read(SVGA_REG_BYTES_PER_LINE);
    const uint64_t fb = vmw_read(SVGA_REG_FB_START);
    uint64_t fbSize = vmw_read(SVGA_REG_FB_SIZE);
    if (actualW != width || actualH != height || !pitch || !fb) return false;
    const uint64_t required = static_cast<uint64_t>(pitch) * height;
    if (fbSize < required) fbSize = required;
    if (!map_mmio_if_needed(fb, fbSize)) return false;

    gBoot->framebuffer = fb;
    gBoot->framebufferSize = fbSize;
    gBoot->width = width;
    gBoot->height = height;
    gBoot->pixelsPerScanLine = pitch / 4u;
    gBoot->pixelFormat = vmware_pixel_format();
    vmw_write(SVGA_REG_SYNC, 1);
    for (uint32_t i = 0; i < 1000000u && vmw_read(SVGA_REG_BUSY); ++i) asm volatile("pause");
    return true;
}

static bool bochs_set_mode(uint32_t width, uint32_t height) {
    const uint64_t needed = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ULL;
    if (!gBochsFb || (gBochsFbSize && needed > gBochsFbSize)) return false;
    if (!map_mmio_if_needed(gBochsFb, needed)) return false;
    bga_write(BGA_REG_ENABLE, 0);
    bga_write(BGA_REG_XRES, static_cast<uint16_t>(width));
    bga_write(BGA_REG_YRES, static_cast<uint16_t>(height));
    bga_write(BGA_REG_BPP, 32);
    bga_write(BGA_REG_VIRT_WIDTH, static_cast<uint16_t>(width));
    bga_write(BGA_REG_X_OFFSET, 0);
    bga_write(BGA_REG_Y_OFFSET, 0);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);
    if (bga_read(BGA_REG_XRES) != width || bga_read(BGA_REG_YRES) != height) return false;

    gBoot->framebuffer = gBochsFb;
    gBoot->framebufferSize = needed;
    gBoot->width = width;
    gBoot->height = height;
    gBoot->pixelsPerScanLine = width;
    gBoot->pixelFormat = 1; // BGRX / 0x00RRGGBB on QEMU stdvga.
    return true;
}

static bool parse_u32(const char*& p, uint32_t& v) {
    v = 0; bool any = false;
    while (*p >= '0' && *p <= '9') { any = true; v = v * 10u + static_cast<uint32_t>(*p - '0'); ++p; }
    return any;
}
static uint32_t append_u32(char* out, uint32_t pos, uint32_t cap, uint32_t v) {
    char r[16]; uint32_t n=0; if (!v) r[n++]='0'; while(v&&n<sizeof(r)){r[n++]=char('0'+v%10u);v/=10u;}
    while(n&&pos+1<cap) out[pos++]=r[--n]; return pos;
}
}

bool display_init(ZeroNTBootInfo* boot) {
    gBoot = boot; gFs = nullptr; gStats = {}; gVmwareIo = 0; gBochsFb = 0; gBochsFbSize = 0; gVmwareFound = gBochsFound = false;
    if (!gBoot || !gBoot->framebuffer || !gBoot->width || !gBoot->height) return false;
    gStats.initialized = true;
    gStats.backend = DisplayBackend::UefiGop;
    gStats.width = gBoot->width; gStats.height = gBoot->height;
    gStats.maxWidth = gBoot->width; gStats.maxHeight = gBoot->height;
    gStats.bitsPerPixel = 32; gStats.framebuffer = gBoot->framebuffer;
    gStats.pitchBytes = gBoot->pixelsPerScanLine * 4u;

    pci_enumerate(detect_pci, nullptr);
    if (!vmware_probe()) (void)bochs_probe();
    return true;
}

bool display_runtime_modes_supported() {
    return gStats.initialized && (gStats.backend == DisplayBackend::VMwareSvgaII || gStats.backend == DisplayBackend::BochsVbe);
}

const char* display_backend_name() {
    switch (gStats.backend) {
        case DisplayBackend::VMwareSvgaII: return "VMWARE SVGA II";
        case DisplayBackend::BochsVbe: return "QEMU BOCHS VBE";
        default: return "UEFI GOP";
    }
}

bool display_set_mode(uint32_t width, uint32_t height) {
    if (!gBoot || !gStats.initialized || !display_runtime_modes_supported()) { ++gStats.failedModeChanges; return false; }
    if (width < 640 || height < 480 || width > gStats.maxWidth || height > gStats.maxHeight || width > 8192 || height > 8192) {
        ++gStats.failedModeChanges; return false;
    }
    // Reject obviously unreasonable aspect/memory combinations before touching hardware.
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (!pixels || pixels > 33554432ULL) { ++gStats.failedModeChanges; return false; }

    bool ok = false;
    if (gStats.backend == DisplayBackend::VMwareSvgaII) ok = vmware_set_mode(width, height);
    else if (gStats.backend == DisplayBackend::BochsVbe) ok = bochs_set_mode(width, height);
    if (!ok) { ++gStats.failedModeChanges; return false; }

    // Rebuild UI backing memory for the new dimensions, clamp existing windows,
    // and force a complete repaint. Direct drawing remains available if PMM is low.
    (void)classic_ui::reconfigure_backbuffer();
    gui_on_screen_resize();
    gStats.runtimeModeSet = true;
    gStats.width = gBoot->width; gStats.height = gBoot->height;
    gStats.framebuffer = gBoot->framebuffer;
    gStats.pitchBytes = gBoot->pixelsPerScanLine * 4u;
    ++gStats.modeChanges;
    event_log_write(EventLevel::Info, "DISPLAY", "RUNTIME RESOLUTION CHANGED", (static_cast<uint64_t>(width)<<32)|height);
    return true;
}

DisplayStats display_stats() { return gStats; }

void display_attach_filesystem(Fat32Volume* volume) { gFs = volume; gStats.persistentConfig = gFs && gFs->stats.mounted; }

bool display_save_mode(uint32_t width, uint32_t height) {
    if (!gFs || !gFs->stats.mounted) return false;
    char cfg[48] = "width="; uint32_t p=6;
    p=append_u32(cfg,p,sizeof(cfg),width); if(p+1<sizeof(cfg))cfg[p++]='\n';
    const char hkey[]="height="; for(uint32_t i=0;hkey[i]&&p+1<sizeof(cfg);++i)cfg[p++]=hkey[i];
    p=append_u32(cfg,p,sizeof(cfg),height); if(p+1<sizeof(cfg))cfg[p++]='\n'; cfg[p]=0;
    const char* path = "/SYSTEM/DISPLAY.CFG";
    Fat32DirEntry e{};
    if (!fat32_stat(*gFs,path,e) && !fat32_create_file(*gFs,path)) return false;
    const bool ok=fat32_write_file(*gFs,path,cfg,p);
    if(ok)gStats.persistentConfig=true;
    return ok;
}

bool display_apply_saved_mode() {
    if (!gFs || !gFs->stats.mounted || !display_runtime_modes_supported()) return false;
    char cfg[64]{}; size_t got=0;
    if(!fat32_read_file(*gFs,"/SYSTEM/DISPLAY.CFG",cfg,sizeof(cfg)-1,got))return false;
    cfg[got]=0; uint32_t w=0,h=0;
    const char* p=cfg;
    while(*p){
        if(p[0]=='w'&&p[1]=='i'&&p[2]=='d'&&p[3]=='t'&&p[4]=='h'&&p[5]=='='){p+=6;(void)parse_u32(p,w);}
        else if(p[0]=='h'&&p[1]=='e'&&p[2]=='i'&&p[3]=='g'&&p[4]=='h'&&p[5]=='t'&&p[6]=='='){p+=7;(void)parse_u32(p,h);}
        else ++p;
    }
    if(!w||!h||(w==gBoot->width&&h==gBoot->height))return false;
    return display_set_mode(w,h);
}
