#pragma once
#include <stdint.h>
#include <stddef.h>
#include "fat32.hpp"

enum class UserRole : uint8_t {
    Guest = 0,
    Standard = 1,
    Administrator = 2,
};

enum UserPermission : uint64_t {
    UserPermReadFiles       = 1ull << 0,
    UserPermWriteOwnFiles   = 1ull << 1,
    UserPermNetwork         = 1ull << 2,
    UserPermAudio           = 1ull << 3,
    UserPermShutdown        = 1ull << 4,
    UserPermChangeSettings  = 1ull << 5,
    UserPermInstallApps     = 1ull << 6,
    UserPermManageUsers     = 1ull << 7,
    UserPermWriteSystem     = 1ull << 8,
};

struct UserSystemStats {
    bool initialized;
    bool setupRequired;
    bool loggedIn;
    uint32_t userCount;
    uint64_t currentUid;
    UserRole currentRole;
    uint64_t permissions;
    char displayName[32];
    char logicalHome[80];
    char backingHome[32];
};

bool user_system_init(Fat32Volume& volume);
bool user_setup_create_admin(Fat32Volume& volume, const char* displayName, const char* password, uint64_t entropy);
bool user_login(Fat32Volume& volume, const char* displayName, const char* password);
void user_logout();
bool user_is_logged_in();
bool user_has_permission(uint64_t permissionMask);
UserSystemStats user_system_stats();

bool user_validate_display_name(const char* name);
bool user_validate_password(const char* password);
bool user_change_display_name(Fat32Volume& volume, const char* newName);
bool user_change_password(Fat32Volume& volume, const char* oldPassword, const char* newPassword, uint64_t entropy);

// Logical zeroNT paths use C:\\Users\\<display name>. Current FAT32 storage is 8.3,
// so each account also has a stable backing directory such as /USERS/U1000.
bool user_resolve_own_path(const char* logicalPath, char* backingPath, size_t capacity, bool writeAccess);
