#pragma once
#include <stdint.h>
#include "block_device.hpp"
#include "fat32.hpp"

using InstallerProgressFn = void (*)(uint64_t copiedSectors, uint64_t totalSectors, void* context);

enum class InstallerTargetState : uint8_t {
    NoTarget,
    Blank,
    ExistingZeroNT,
    Installed,
};

struct InstallerStats {
    bool initialized;
    bool sourceReady;
    bool targetReady;
    bool installAttempted;
    bool installSucceeded;
    bool verifySucceeded;
    InstallerTargetState state;
    uint64_t sourceSectors;
    uint64_t targetSectors;
    uint64_t copiedSectors;
    bool gptCreated;
    bool espFormatted;
    bool bootFilesInstalled;
    bool repairSucceeded;
    bool upgradeSucceeded;
};

bool installer_init(BlockDevice* sourceTemplate, BlockDevice* targetDisk);
InstallerTargetState installer_probe_target();
bool installer_needs_install();
bool installer_install(InstallerProgressFn progress, void* context);
bool installer_repair();
bool installer_upgrade(InstallerProgressFn progress, void* context);
bool installer_mount_target(Fat32Volume& outVolume);
bool installer_verify_target();
InstallerStats installer_stats();
