#pragma once
#include <stdint.h>
#include "../../shared/bootinfo.hpp"
#include "fat32.hpp"

enum class SystemAppId : uint8_t {
    Desktop = 0,
    Explorer,
    Notepad,
    CommandPrompt,
    ControlPanel,
    TaskManager,
    DeviceManager,
    RecycleBin,
    Services,
    EventViewer,
    MyComputer,
    NetworkNeighborhood,
    Documents,
    Printers,
};

struct SystemAppsStats {
    bool initialized;
    SystemAppId active;
    uint32_t explorerItems;
    uint32_t recycleItems;
    uint64_t notepadSaves;
    uint64_t commandsExecuted;
};

bool system_apps_init(ZeroNTBootInfo* boot, Fat32Volume* volume);
void system_apps_on_login();
void system_apps_on_logout();
void system_apps_set_modifiers(bool shift, bool ctrl, bool alt);
void system_apps_key(uint8_t scancode, char ascii);
void system_apps_mouse(int32_t x, int32_t y, uint8_t buttons);
void system_apps_wheel(int32_t x, int32_t y, int8_t delta);
void system_apps_render(int32_t mouseX, int32_t mouseY);
bool system_apps_dirty();
void system_apps_mark_dirty();
bool system_apps_take_logout_request();
SystemAppsStats system_apps_stats();

// Ring-3 .EXE launch bridge. The executable performs SysShellOpenApp and the
// shell opens the requested system application after validating the app id.
bool system_apps_shell_open(uint32_t appId);
bool system_apps_launch(SystemAppId app);
const char* system_apps_executable_path(SystemAppId app);
