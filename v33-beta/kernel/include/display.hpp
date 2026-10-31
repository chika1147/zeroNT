#pragma once
#include <stdint.h>
#include "../../shared/bootinfo.hpp"

struct Fat32Volume;

enum class DisplayBackend : uint8_t {
    UefiGop = 0,
    VMwareSvgaII = 1,
    BochsVbe = 2,
};

struct DisplayStats {
    bool initialized;
    bool runtimeModeSet;
    bool persistentConfig;
    DisplayBackend backend;
    uint32_t width;
    uint32_t height;
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint32_t bitsPerPixel;
    uint64_t framebuffer;
    uint32_t pitchBytes;
    uint64_t modeChanges;
    uint64_t failedModeChanges;
};

bool display_init(ZeroNTBootInfo* boot);
bool display_set_mode(uint32_t width, uint32_t height);
bool display_runtime_modes_supported();
const char* display_backend_name();
DisplayStats display_stats();

// Once the installed FAT32 volume is available, Settings can persist the
// chosen resolution in /SYSTEM/DISPLAY.CFG and the kernel reapplies it on boot.
void display_attach_filesystem(Fat32Volume* volume);
bool display_save_mode(uint32_t width, uint32_t height);
bool display_apply_saved_mode();
