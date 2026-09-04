/* NTZero Alpha 0.1 UEFI loader.
 * Loads the embedded ntoskrnl.exe PE32+ image at its preferred ImageBase,
 * exits UEFI boot services, and transfers control to KiSystemStartup.
 */

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef UINT64             UINTN;
typedef UINT64             EFI_STATUS;
typedef void              *EFI_HANDLE;
typedef UINT16             CHAR16;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (__attribute__((ms_abi)) *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *Reset;
    EFI_TEXT_STRING OutputString;
};

typedef EFI_STATUS (__attribute__((ms_abi)) *EFI_ALLOCATE_PAGES)(UINT32, UINT32, UINTN, UINT64 *);
typedef EFI_STATUS (__attribute__((ms_abi)) *EFI_FREE_PAGES)(UINT64, UINTN);
typedef EFI_STATUS (__attribute__((ms_abi)) *EFI_GET_MEMORY_MAP)(UINTN *, void *, UINTN *, UINTN *, UINT32 *);
typedef EFI_STATUS (__attribute__((ms_abi)) *EFI_ALLOCATE_POOL)(UINT32, UINTN, void **);
typedef EFI_STATUS (__attribute__((ms_abi)) *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (__attribute__((ms_abi)) *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    void *HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

#define EFI_SUCCESS 0ULL
#define EFI_BUFFER_TOO_SMALL 5ULL
#define EFI_INVALID_PARAMETER 2ULL
#define AllocateAddress 2u
#define EfiLoaderData 2u
#define NTZERO_BOOT_MAGIC 0x4F52455A544E544EULL

extern const UINT8 g_ntzero_kernel_blob[];
extern const UINT64 g_ntzero_kernel_blob_size;

typedef struct NTZ_BOOT_INFO {
    UINT64 magic;
    UINT64 version;
    void *uefi_memory_map;
    UINT64 uefi_memory_map_size;
    UINT64 uefi_descriptor_size;
    UINT64 uefi_descriptor_version;
} NTZ_BOOT_INFO;

typedef void (__attribute__((ms_abi)) *KERNEL_ENTRY)(NTZ_BOOT_INFO *);

static void mem_zero(void *dst, UINTN size) {
    UINT8 *d = (UINT8 *)dst;
    while (size-- != 0u) {
        *d++ = 0u;
    }
}

static void mem_copy(void *dst, const void *src, UINTN size) {
    UINT8 *d = (UINT8 *)dst;
    const UINT8 *s = (const UINT8 *)src;
    while (size-- != 0u) {
        *d++ = *s++;
    }
}

static UINT16 read16(const UINT8 *p) {
    return (UINT16)((UINT16)p[0] | ((UINT16)p[1] << 8));
}

static UINT32 read32(const UINT8 *p) {
    return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static UINT64 read64(const UINT8 *p) {
    UINT64 lo = read32(p);
    UINT64 hi = read32(p + 4);
    return lo | (hi << 32);
}

static void print(EFI_SYSTEM_TABLE *st, CHAR16 *text) {
    if (st != (EFI_SYSTEM_TABLE *)0 && st->ConOut != (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *)0 && st->ConOut->OutputString != (EFI_TEXT_STRING)0) {
        (void)st->ConOut->OutputString(st->ConOut, text);
    }
}

static EFI_STATUS load_kernel(EFI_BOOT_SERVICES *bs, UINT64 *image_base, UINT32 *entry_rva) {
    const UINT8 *image = g_ntzero_kernel_blob;
    UINT32 pe_offset;
    const UINT8 *coff;
    const UINT8 *opt;
    const UINT8 *section;
    UINT16 section_count;
    UINT16 optional_size;
    UINT64 preferred_base;
    UINT32 size_of_image;
    UINT32 size_of_headers;
    UINT64 load_address;
    UINTN page_count;
    UINT16 i;
    EFI_STATUS status;

    if (g_ntzero_kernel_blob_size < 0x100u || read16(image) != 0x5A4Du) {
        return EFI_INVALID_PARAMETER;
    }
    pe_offset = read32(image + 0x3Cu);
    if ((UINT64)pe_offset + 0x108u >= g_ntzero_kernel_blob_size || read32(image + pe_offset) != 0x00004550u) {
        return EFI_INVALID_PARAMETER;
    }
    coff = image + pe_offset + 4u;
    section_count = read16(coff + 2u);
    optional_size = read16(coff + 16u);
    opt = coff + 20u;
    if (read16(opt) != 0x20Bu || optional_size < 0x70u) {
        return EFI_INVALID_PARAMETER;
    }

    *entry_rva = read32(opt + 16u);
    preferred_base = read64(opt + 24u);
    size_of_image = read32(opt + 56u);
    size_of_headers = read32(opt + 60u);
    if (preferred_base == 0u || size_of_image == 0u || size_of_headers == 0u) {
        return EFI_INVALID_PARAMETER;
    }

    load_address = preferred_base;
    page_count = ((UINTN)size_of_image + 4095u) / 4096u;
    status = bs->AllocatePages(AllocateAddress, EfiLoaderData, page_count, &load_address);
    if (status != EFI_SUCCESS || load_address != preferred_base) {
        return status == EFI_SUCCESS ? EFI_INVALID_PARAMETER : status;
    }

    mem_zero((void *)(UINTN)load_address, page_count * 4096u);
    if ((UINT64)size_of_headers > g_ntzero_kernel_blob_size) {
        return EFI_INVALID_PARAMETER;
    }
    mem_copy((void *)(UINTN)load_address, image, size_of_headers);

    section = opt + optional_size;
    for (i = 0u; i < section_count; ++i) {
        UINT32 virtual_size = read32(section + 8u);
        UINT32 virtual_address = read32(section + 12u);
        UINT32 raw_size = read32(section + 16u);
        UINT32 raw_offset = read32(section + 20u);
        UINT32 copy_size = raw_size;
        if (virtual_size != 0u && copy_size > virtual_size) {
            copy_size = virtual_size;
        }
        if ((UINT64)virtual_address + copy_size > size_of_image || (UINT64)raw_offset + copy_size > g_ntzero_kernel_blob_size) {
            return EFI_INVALID_PARAMETER;
        }
        if (copy_size != 0u) {
            mem_copy((void *)(UINTN)(load_address + virtual_address), image + raw_offset, copy_size);
        }
        section += 40u;
    }

    *image_base = load_address;
    return EFI_SUCCESS;
}

EFI_STATUS __attribute__((ms_abi)) EfiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    EFI_BOOT_SERVICES *bs;
    EFI_STATUS status;
    UINT64 kernel_base = 0u;
    UINT32 kernel_entry_rva = 0u;
    UINTN map_size = 0u;
    UINTN map_key = 0u;
    UINTN descriptor_size = 0u;
    UINT32 descriptor_version = 0u;
    void *map = (void *)0;
    NTZ_BOOT_INFO boot_info;
    KERNEL_ENTRY kernel_entry;

    if (system_table == (EFI_SYSTEM_TABLE *)0 || system_table->BootServices == (EFI_BOOT_SERVICES *)0) {
        return EFI_INVALID_PARAMETER;
    }
    bs = system_table->BootServices;
    print(system_table, L"NTZero Alpha 0.1 Boot\r\n");

    status = load_kernel(bs, &kernel_base, &kernel_entry_rva);
    if (status != EFI_SUCCESS) {
        print(system_table, L"Kernel PE load failed.\r\n");
        return status;
    }
    print(system_table, L"ntoskrnl.exe mapped.\r\n");

    status = bs->GetMemoryMap(&map_size, (void *)0, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size == 0u) {
        return status;
    }
    map_size += descriptor_size * 8u;
    status = bs->AllocatePool(EfiLoaderData, map_size, &map);
    if (status != EFI_SUCCESS) {
        return status;
    }

    status = bs->GetMemoryMap(&map_size, map, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_SUCCESS) {
        return status;
    }

    boot_info.magic = NTZERO_BOOT_MAGIC;
    boot_info.version = 0x00010000u;
    boot_info.uefi_memory_map = map;
    boot_info.uefi_memory_map_size = map_size;
    boot_info.uefi_descriptor_size = descriptor_size;
    boot_info.uefi_descriptor_version = descriptor_version;

    status = bs->ExitBootServices(image_handle, map_key);
    if (status == EFI_INVALID_PARAMETER) {
        status = bs->GetMemoryMap(&map_size, map, &map_key, &descriptor_size, &descriptor_version);
        if (status == EFI_SUCCESS) {
            boot_info.uefi_memory_map_size = map_size;
            boot_info.uefi_descriptor_size = descriptor_size;
            boot_info.uefi_descriptor_version = descriptor_version;
            status = bs->ExitBootServices(image_handle, map_key);
        }
    }
    if (status != EFI_SUCCESS) {
        return status;
    }

    kernel_entry = (KERNEL_ENTRY)(UINTN)(kernel_base + kernel_entry_rva);
    kernel_entry(&boot_info);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
