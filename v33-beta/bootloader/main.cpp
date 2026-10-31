#include "efi.hpp"
#include "../shared/bootinfo.hpp"

// zeroNT stage-1 ELF64 PIE kernel loader v33.0.
// Loads the kernel and a FAT32 rootfs image before ExitBootServices.
// The kernel consumes rootfs.img as a raw block device and parses FAT32 itself.

enum EFI_ALLOCATE_TYPE : uint32_t {
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2
};

enum EFI_MEMORY_TYPE : uint32_t {
    EfiReservedMemoryType = 0,
    EfiLoaderCode = 1,
    EfiLoaderData = 2,
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiRuntimeServicesCode = 5,
    EfiRuntimeServicesData = 6,
    EfiConventionalMemory = 7
};

struct EFI_MEMORY_DESCRIPTOR {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
};

using EFI_ALLOCATE_PAGES = EFI_STATUS (EFIAPI *)(EFI_ALLOCATE_TYPE, EFI_MEMORY_TYPE, size_t, EFI_PHYSICAL_ADDRESS*);
using EFI_GET_MEMORY_MAP = EFI_STATUS (EFIAPI *)(size_t*, EFI_MEMORY_DESCRIPTOR*, size_t*, size_t*, uint32_t*);
using EFI_FREE_PAGES = EFI_STATUS (EFIAPI *)(EFI_PHYSICAL_ADDRESS, size_t);
using EFI_FREE_POOL = EFI_STATUS (EFIAPI *)(void*);
using EFI_EXIT_BOOT_SERVICES = EFI_STATUS (EFIAPI *)(EFI_HANDLE, size_t);
using EFI_HANDLE_PROTOCOL_FN = EFI_STATUS (EFIAPI *)(EFI_HANDLE, EFI_GUID*, void**);

static constexpr EFI_STATUS EFI_BUFFER_TOO_SMALL = 0x8000000000000005ULL;
static constexpr EFI_STATUS EFI_NOT_FOUND = 0x800000000000000EULL;
static constexpr uint32_t EFI_VARIABLE_NON_VOLATILE = 0x00000001u;
static constexpr uint32_t EFI_VARIABLE_BOOTSERVICE_ACCESS = 0x00000002u;
static constexpr uint32_t EFI_VARIABLE_RUNTIME_ACCESS = 0x00000004u;
static constexpr uint32_t LOAD_OPTION_ACTIVE = 0x00000001u;
static const EFI_GUID EFI_GLOBAL_VARIABLE_GUID = {0x8BE4DF61,0x93CA,0x11D2,{0xAA,0x0D,0x00,0xE0,0x98,0x03,0x2B,0x8C}};

static EFI_SYSTEM_TABLE* gST = nullptr;

extern "C" void* memset(void* dst, int value, size_t count) {
    auto* p = static_cast<unsigned char*>(dst);
    for (size_t i = 0; i < count; ++i) p[i] = static_cast<unsigned char>(value);
    return dst;
}

extern "C" void* memcpy(void* dst, const void* src, size_t count) {
    auto* d = static_cast<unsigned char*>(dst);
    auto* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return dst;
}

static void print(const wchar_t* s) {
    if (gST && gST->ConOut && gST->ConOut->OutputString)
        gST->ConOut->OutputString(gST->ConOut, reinterpret_cast<const CHAR16*>(s));
}

static void print_hex64(uint64_t value) {
    wchar_t buf[19];
    buf[0] = L'0'; buf[1] = L'x';
    const wchar_t* hex = L"0123456789ABCDEF";
    for (int i = 0; i < 16; ++i) {
        const int shift = (15 - i) * 4;
        buf[2 + i] = hex[(value >> shift) & 0xF];
    }
    buf[18] = 0;
    print(buf);
}

static void print_status(const wchar_t* label, EFI_STATUS s) {
    print(label); print(L": "); print_hex64(s); print(L"\r\n");
}

static bool guid_equal(const EFI_GUID& a, const EFI_GUID& b) {
    if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3) return false;
    for (int i = 0; i < 8; ++i) if (a.Data4[i] != b.Data4[i]) return false;
    return true;
}

static void* find_acpi_rsdp(EFI_SYSTEM_TABLE* st) {
    const EFI_GUID acpi20 = {0x8868e871, 0xe4f1, 0x11d3, {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
    const EFI_GUID acpi10 = {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    void* acpi1 = nullptr;
    for (size_t i = 0; i < st->NumberOfTableEntries; ++i) {
        if (guid_equal(st->ConfigurationTable[i].VendorGuid, acpi20)) return st->ConfigurationTable[i].VendorTable;
        if (guid_equal(st->ConfigurationTable[i].VendorGuid, acpi10)) acpi1 = st->ConfigurationTable[i].VendorTable;
    }
    return acpi1;
}

struct FsContext { EFI_FILE_PROTOCOL* root; };

static bool open_root(EFI_HANDLE imageHandle, FsContext& fs) {
    auto handleProtocol = reinterpret_cast<EFI_HANDLE_PROTOCOL_FN>(gST->BootServices->HandleProtocol);
    EFI_LOADED_IMAGE_PROTOCOL* loaded = nullptr;
    EFI_STATUS s = handleProtocol(imageHandle, &EFI_LOADED_IMAGE_PROTOCOL_GUID, reinterpret_cast<void**>(&loaded));
    if (EFI_ERROR(s) || !loaded) return false;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* simpleFs = nullptr;
    s = handleProtocol(loaded->DeviceHandle, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, reinterpret_cast<void**>(&simpleFs));
    if (EFI_ERROR(s) || !simpleFs) return false;
    fs.root = nullptr;
    s = simpleFs->OpenVolume(simpleFs, &fs.root);
    return !EFI_ERROR(s) && fs.root;
}


static uint16_t dp_node_length(const EFI_DEVICE_PATH_PROTOCOL* n) {
    return n ? static_cast<uint16_t>(n->Length[0] | (static_cast<uint16_t>(n->Length[1])<<8)) : 0;
}

static size_t device_path_size(const EFI_DEVICE_PATH_PROTOCOL* path) {
    if(!path)return 0;
    size_t total=0;
    const auto* n=path;
    for(uint32_t guard=0;guard<256;++guard){
        const uint16_t len=dp_node_length(n);
        if(len<4||len>4096)return 0;
        total+=len;
        if(n->Type==0x7Fu&&n->SubType==0xFFu)return total;
        n=reinterpret_cast<const EFI_DEVICE_PATH_PROTOCOL*>(reinterpret_cast<const uint8_t*>(n)+len);
    }
    return 0;
}

static void boot_var_name(uint16_t number,CHAR16 out[9]) {
    static const CHAR16 hex[16]={L'0',L'1',L'2',L'3',L'4',L'5',L'6',L'7',L'8',L'9',L'A',L'B',L'C',L'D',L'E',L'F'};
    out[0]=L'B';out[1]=L'o';out[2]=L'o';out[3]=L't';
    out[4]=hex[(number>>12)&0xF];out[5]=hex[(number>>8)&0xF];out[6]=hex[(number>>4)&0xF];out[7]=hex[number&0xF];out[8]=0;
}

static bool load_option_is_zeront(const uint8_t* data,size_t bytes) {
    if(!data||bytes<8)return false;
    const CHAR16* d=reinterpret_cast<const CHAR16*>(data+6);
    const CHAR16 want[]={L'z',L'e',L'r',L'o',L'N',L'T',0};
    for(size_t i=0;i<7;++i)if(d[i]!=want[i])return false;
    return true;
}

static bool register_zeront_boot_entry(EFI_HANDLE imageHandle) {
    if(!gST||!gST->RuntimeServices||!gST->RuntimeServices->GetVariable||!gST->RuntimeServices->SetVariable)return false;
    auto handleProtocol=reinterpret_cast<EFI_HANDLE_PROTOCOL_FN>(gST->BootServices->HandleProtocol);
    EFI_LOADED_IMAGE_PROTOCOL* loaded=nullptr;
    if(EFI_ERROR(handleProtocol(imageHandle,&EFI_LOADED_IMAGE_PROTOCOL_GUID,reinterpret_cast<void**>(&loaded)))||!loaded||!loaded->DeviceHandle||!loaded->FilePath)return false;
    EFI_DEVICE_PATH_PROTOCOL* devicePath=nullptr;
    if(EFI_ERROR(handleProtocol(loaded->DeviceHandle,&EFI_DEVICE_PATH_PROTOCOL_GUID,reinterpret_cast<void**>(&devicePath)))||!devicePath)return false;

    const size_t deviceBytes=device_path_size(devicePath);
    const size_t fileBytes=device_path_size(static_cast<EFI_DEVICE_PATH_PROTOCOL*>(loaded->FilePath));
    if(deviceBytes<4||fileBytes<4||deviceBytes+fileBytes>2048)return false;
    const size_t fullPathBytes=deviceBytes-4+fileBytes;
    static uint8_t fullPath[2048];
    memset(fullPath,0,sizeof(fullPath));
    memcpy(fullPath,devicePath,deviceBytes-4);
    memcpy(fullPath+deviceBytes-4,loaded->FilePath,fileBytes);

    static uint16_t order[256];memset(order,0,sizeof(order));size_t orderBytes=sizeof(order);uint32_t attrs=0;
    EFI_STATUS os=gST->RuntimeServices->GetVariable(const_cast<CHAR16*>(reinterpret_cast<const CHAR16*>(L"BootOrder")),const_cast<EFI_GUID*>(&EFI_GLOBAL_VARIABLE_GUID),&attrs,&orderBytes,order);
    if(EFI_ERROR(os)){orderBytes=0;}
    const size_t orderCount=orderBytes/sizeof(uint16_t);

    static uint8_t optionBuf[4096];memset(optionBuf,0,sizeof(optionBuf));
    // Reuse an existing zeroNT entry if one is already present.
    for(size_t i=0;i<orderCount;++i){
        CHAR16 n[9]{};boot_var_name(order[i],n);size_t sz=sizeof(optionBuf);uint32_t a=0;
        EFI_STATUS st=gST->RuntimeServices->GetVariable(n,const_cast<EFI_GUID*>(&EFI_GLOBAL_VARIABLE_GUID),&a,&sz,optionBuf);
        if(!EFI_ERROR(st)&&load_option_is_zeront(optionBuf,sz))return true;
    }

    uint16_t number=0xFFFFu;
    for(uint32_t candidate=0;candidate<0x10000u;++candidate){
        CHAR16 n[9]{};boot_var_name(static_cast<uint16_t>(candidate),n);uint8_t tiny[8]{};size_t sz=sizeof(tiny);uint32_t a=0;
        EFI_STATUS st=gST->RuntimeServices->GetVariable(n,const_cast<EFI_GUID*>(&EFI_GLOBAL_VARIABLE_GUID),&a,&sz,tiny);
        if(st==EFI_NOT_FOUND){number=static_cast<uint16_t>(candidate);break;}
    }
    if(number==0xFFFFu)return false;

    const CHAR16 description[]={L'z',L'e',L'r',L'o',L'N',L'T',0};
    const size_t descBytes=sizeof(description);
    const size_t optionBytes=6+descBytes+fullPathBytes;
    if(optionBytes>sizeof(optionBuf))return false;
    memset(optionBuf,0,sizeof(optionBuf));
    *reinterpret_cast<uint32_t*>(optionBuf)=LOAD_OPTION_ACTIVE;
    *reinterpret_cast<uint16_t*>(optionBuf+4)=static_cast<uint16_t>(fullPathBytes);
    memcpy(optionBuf+6,description,descBytes);
    memcpy(optionBuf+6+descBytes,fullPath,fullPathBytes);
    CHAR16 bootName[9]{};boot_var_name(number,bootName);
    const uint32_t varAttrs=EFI_VARIABLE_NON_VOLATILE|EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS;
    if(EFI_ERROR(gST->RuntimeServices->SetVariable(bootName,const_cast<EFI_GUID*>(&EFI_GLOBAL_VARIABLE_GUID),varAttrs,optionBytes,optionBuf)))return false;

    static uint16_t newOrder[256];memset(newOrder,0,sizeof(newOrder));size_t newCount=0;newOrder[newCount++]=number;
    for(size_t i=0;i<orderCount&&newCount<256;++i)if(order[i]!=number)newOrder[newCount++]=order[i];
    CHAR16 bootOrderName[]={L'B',L'o',L'o',L't',L'O',L'r',L'd',L'e',L'r',0};
    if(EFI_ERROR(gST->RuntimeServices->SetVariable(bootOrderName,const_cast<EFI_GUID*>(&EFI_GLOBAL_VARIABLE_GUID),varAttrs,newCount*sizeof(uint16_t),newOrder)))return false;
    return true;
}

static size_t cstrlen(const char* s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static bool starts_with(const char* s, const char* prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return false;
        ++i;
    }
    return true;
}

static void ascii_to_char16(const char* src, CHAR16* dst, size_t cap) {
    if (!cap) return;
    size_t i = 0;
    for (; src && src[i] && i + 1 < cap; ++i)
        dst[i] = static_cast<CHAR16>(static_cast<unsigned char>(src[i]));
    dst[i] = 0;
}

static bool ascii_ieq(const char* a, const char* b) {
    if (!a || !b) return false;
    for (;;) {
        char x = *a++, y = *b++;
        if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
        if (x != y) return false;
        if (!x) return true;
    }
}

static uint32_t parse_u32(const char*& p) {
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        const uint32_t d = static_cast<uint32_t>(*p - '0');
        if (v > 429496729u || (v == 429496729u && d > 5u)) return 0;
        v = v * 10u + d;
        ++p;
    }
    return v;
}

static void load_config_ascii(FsContext& fs, const char* key, const char* fallback, char* out, size_t cap) {
    if (!out || !cap) return;
    size_t oi = 0;
    if (fallback) while (fallback[oi] && oi + 1 < cap) { out[oi] = fallback[oi]; ++oi; }
    out[oi] = 0;

    CHAR16 cfgPath[64];
    ascii_to_char16("\\Config\\boot.cfg", cfgPath, 64);
    EFI_FILE_PROTOCOL* f = nullptr;
    EFI_STATUS s = fs.root->Open(fs.root, &f, cfgPath, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s) || !f) return;

    char buf[1024];
    size_t sz = sizeof(buf) - 1;
    s = f->Read(f, &sz, buf);
    f->Close(f);
    if (EFI_ERROR(s)) return;
    buf[sz] = 0;

    const size_t keyLen = cstrlen(key);
    for (size_t i = 0; i < sz; ) {
        const size_t lineStart = i;
        while (i < sz && buf[i] != '\r' && buf[i] != '\n') ++i;
        const size_t lineEnd = i;
        while (i < sz && (buf[i] == '\r' || buf[i] == '\n')) ++i;
        if (lineEnd <= lineStart) continue;
        char saved = buf[lineEnd];
        buf[lineEnd] = 0;
        const char* line = &buf[lineStart];
        if (starts_with(line, key) && line[keyLen] == '=') {
            const char* value = line + keyLen + 1;
            size_t n = 0;
            while (value[n] && n + 1 < cap) { out[n] = value[n]; ++n; }
            out[n] = 0;
            buf[lineEnd] = saved;
            return;
        }
        buf[lineEnd] = saved;
    }
}


static uint32_t crc32_bytes(const void* data, uint64_t bytes) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (uint64_t i = 0; i < bytes; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static bool parse_hex32_text(const char* text, uint32_t& out) {
    out = 0;
    if (!text || !*text) return false;
    const char* p = text;
    if (p[0]=='0' && (p[1]=='x' || p[1]=='X')) p += 2;
    bool any = false;
    while (*p) {
        uint8_t d = 0;
        if (*p>='0'&&*p<='9') d = static_cast<uint8_t>(*p-'0');
        else if (*p>='a'&&*p<='f') d = static_cast<uint8_t>(*p-'a'+10);
        else if (*p>='A'&&*p<='F') d = static_cast<uint8_t>(*p-'A'+10);
        else return false;
        if (out > 0x0FFFFFFFu) return false;
        out = (out << 4) | d; any = true; ++p;
    }
    return any;
}

static uint32_t load_config_hex32(FsContext& fs, const char* key) {
    char text[32]{};
    load_config_ascii(fs, key, "", text, sizeof(text));
    uint32_t v = 0;
    return parse_hex32_text(text, v) ? v : 0;
}
static bool parse_video_mode(const char* text, uint32_t& width, uint32_t& height) {
    width = height = 0;
    if (!text) return false;
    const char* p = text;
    const uint32_t w = parse_u32(p);
    if (!w || (*p != 'x' && *p != 'X')) return false;
    ++p;
    const uint32_t h = parse_u32(p);
    if (!h || *p) return false;
    width = w; height = h;
    return true;
}

static void print_u32(uint32_t value) {
    wchar_t buf[16];
    wchar_t rev[16];
    size_t n = 0;
    if (!value) rev[n++] = L'0';
    while (value && n < 15) { rev[n++] = static_cast<wchar_t>(L'0' + (value % 10u)); value /= 10u; }
    size_t o = 0;
    while (n) buf[o++] = rev[--n];
    buf[o] = 0;
    print(buf);
}

static bool choose_graphics_mode(FsContext& fs, EFI_GRAPHICS_OUTPUT_PROTOCOL* gop) {
    if (!gop || !gop->Mode || !gop->Mode->Info || !gop->QueryMode || !gop->SetMode) return false;

    char requested[32];
    load_config_ascii(fs, "video", "auto", requested, sizeof(requested));
    if (ascii_ieq(requested, "current")) return true;

    uint32_t exactW = 0, exactH = 0;
    const bool exact = parse_video_mode(requested, exactW, exactH);
    const bool automatic = ascii_ieq(requested, "auto") || !exact;
    const uint32_t currentMode = gop->Mode->Mode;
    uint32_t bestMode = currentMode;
    uint64_t bestScore = 0;
    bool found = false;
    auto freePool = reinterpret_cast<EFI_FREE_POOL>(gST->BootServices->FreePool);

    for (uint32_t mode = 0; mode < gop->Mode->MaxMode; ++mode) {
        size_t infoSize = 0;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = nullptr;
        EFI_STATUS qs = gop->QueryMode(gop, mode, &infoSize, &info);
        if (EFI_ERROR(qs) || !info) continue;

        const uint32_t w = info->HorizontalResolution;
        const uint32_t h = info->VerticalResolution;
        const bool directFb = info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
                              info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor;
        const bool sane = directFb && w >= 640 && h >= 480 && info->PixelsPerScanLine >= w;
        bool take = false;
        uint64_t score = 0;
        if (sane && exact) {
            take = (w == exactW && h == exactH);
            score = take ? 1 : 0;
        } else if (sane && automatic) {
            // Keep automatic modes responsive with the current software renderer.
            // Explicit video=WIDTHxHEIGHT can still request another supported GOP mode.
            if (w <= 1920 && h <= 1200) {
                score = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
                // Prefer 16:9/16:10/4:3-ish modes over unusual firmware modes when pixel counts tie.
                const uint64_t ratio = static_cast<uint64_t>(w) * 1000ULL / h;
                if (ratio >= 1300 && ratio <= 1800) score += 100000000ULL;
                take = !found || score > bestScore;
            }
        }

        if (take) { bestMode = mode; bestScore = score; found = true; }
        if (freePool) freePool(info);
        if (exact && found) break;
    }

    // If an exact request was unavailable, fall back to the firmware's current mode
    // instead of failing the boot. Automatic mode also safely keeps current mode if
    // no suitable direct-framebuffer mode was enumerated.
    if (!found) return true;
    if (bestMode != currentMode) {
        EFI_STATUS ss = gop->SetMode(gop, bestMode);
        if (EFI_ERROR(ss)) return false;
    }
    return gop->Mode && gop->Mode->Info && gop->Mode->FrameBufferBase;
}

static void load_config_path(FsContext& fs, const char* key, const char* fallback, CHAR16* outPath, size_t outChars) {
    ascii_to_char16(fallback, outPath, outChars);
    CHAR16 cfgPath[64];
    ascii_to_char16("\\Config\\boot.cfg", cfgPath, 64);
    EFI_FILE_PROTOCOL* f = nullptr;
    EFI_STATUS s = fs.root->Open(fs.root, &f, cfgPath, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s) || !f) return;

    char buf[1024];
    size_t sz = sizeof(buf) - 1;
    s = f->Read(f, &sz, buf);
    f->Close(f);
    if (EFI_ERROR(s)) return;
    buf[sz] = 0;

    const size_t keyLen = cstrlen(key);
    for (size_t i = 0; i < sz; ) {
        const size_t lineStart = i;
        while (i < sz && buf[i] != '\r' && buf[i] != '\n') ++i;
        const size_t lineEnd = i;
        while (i < sz && (buf[i] == '\r' || buf[i] == '\n')) ++i;
        if (lineEnd <= lineStart) continue;
        char saved = buf[lineEnd];
        buf[lineEnd] = 0;
        const char* line = &buf[lineStart];
        if (starts_with(line, key) && line[keyLen] == '=') {
            const char* value = line + keyLen + 1;
            if (cstrlen(value)) ascii_to_char16(value, outPath, outChars);
            buf[lineEnd] = saved;
            return;
        }
        buf[lineEnd] = saved;
    }
}

static EFI_STATUS get_file_size_robust(EFI_FILE_PROTOCOL* f, uint64_t& sizeOut) {
    sizeOut = 0;
    // UEFI specifies SetPosition(~0ULL) as seek-to-end for normal files.
    EFI_STATUS s = f->SetPosition(f, ~0ULL);
    if (!EFI_ERROR(s)) {
        uint64_t pos = 0;
        s = f->GetPosition(f, &pos);
        if (!EFI_ERROR(s) && pos) {
            sizeOut = pos;
            f->SetPosition(f, 0);
            return EFI_SUCCESS;
        }
        f->SetPosition(f, 0);
    }

    alignas(8) uint8_t infoBuf[1024];
    size_t infoSize = sizeof(infoBuf);
    s = f->GetInfo(f, &EFI_FILE_INFO_GUID, &infoSize, infoBuf);
    if (EFI_ERROR(s)) return s;
    auto* info = reinterpret_cast<EFI_FILE_INFO*>(infoBuf);
    sizeOut = info->FileSize;
    f->SetPosition(f, 0);
    return sizeOut ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static EFI_STATUS open_read_candidate(FsContext& fs, CHAR16* primary, const char* fallback1,
                                      const char* fallback2, EFI_FILE_PROTOCOL** out) {
    *out = nullptr;
    EFI_STATUS s = fs.root->Open(fs.root, out, primary, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(s) && *out) return EFI_SUCCESS;

    CHAR16 p1[256]; ascii_to_char16(fallback1, p1, 256);
    s = fs.root->Open(fs.root, out, p1, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(s) && *out) return EFI_SUCCESS;

    CHAR16 p2[256]; ascii_to_char16(fallback2, p2, 256);
    s = fs.root->Open(fs.root, out, p2, EFI_FILE_MODE_READ, 0);
    return s;
}

static EFI_STATUS read_exact_chunks(EFI_FILE_PROTOCOL* f, void* dst, uint64_t bytes) {
    auto* out = static_cast<uint8_t*>(dst);
    uint64_t done = 0;
    while (done < bytes) {
        size_t chunk = static_cast<size_t>(bytes - done);
        if (chunk > 1024 * 1024) chunk = 1024 * 1024;
        size_t got = chunk;
        EFI_STATUS s = f->Read(f, &got, out + done);
        if (EFI_ERROR(s)) return s;
        if (!got) return EFI_NOT_FOUND;
        done += got;
    }
    return EFI_SUCCESS;
}


#pragma pack(push,1)
struct Elf64Ehdr {
    uint8_t ident[16]; uint16_t type; uint16_t machine; uint32_t version;
    uint64_t entry; uint64_t phoff; uint64_t shoff; uint32_t flags;
    uint16_t ehsize; uint16_t phentsize; uint16_t phnum;
    uint16_t shentsize; uint16_t shnum; uint16_t shstrndx;
};
struct Elf64Phdr {
    uint32_t type; uint32_t flags; uint64_t offset; uint64_t vaddr; uint64_t paddr;
    uint64_t filesz; uint64_t memsz; uint64_t align;
};
struct Elf64Shdr {
    uint32_t name; uint32_t type; uint64_t flags; uint64_t addr; uint64_t offset;
    uint64_t size; uint32_t link; uint32_t info; uint64_t addralign; uint64_t entsize;
};
struct Elf64Rela { uint64_t offset; uint64_t info; int64_t addend; };
#pragma pack(pop)

static constexpr uint16_t ELF_ET_DYN = 3;
static constexpr uint16_t ELF_EM_X86_64 = 62;
static constexpr uint32_t ELF_PT_LOAD = 1;
static constexpr uint32_t ELF_SHT_RELA = 4;
static constexpr uint32_t ELF_R_X86_64_RELATIVE = 8;

static bool elf_header_ok(const Elf64Ehdr* e, uint64_t bytes) {
    if (!e || bytes < sizeof(Elf64Ehdr)) return false;
    return e->ident[0]==0x7F && e->ident[1]=='E' && e->ident[2]=='L' && e->ident[3]=='F' &&
           e->ident[4]==2 && e->ident[5]==1 && e->type==ELF_ET_DYN && e->machine==ELF_EM_X86_64 &&
           e->phentsize==sizeof(Elf64Phdr) && e->phoff + uint64_t(e->phnum)*sizeof(Elf64Phdr) <= bytes;
}


static bool is_pow2_64(uint64_t v) { return v && ((v & (v - 1)) == 0); }

static EFI_STATUS allocate_kernel_region_from_memory_map(size_t pages, EFI_PHYSICAL_ADDRESS& base) {
    base = 0;
    auto allocatePages = reinterpret_cast<EFI_ALLOCATE_PAGES>(gST->BootServices->AllocatePages);
    auto getMemoryMap = reinterpret_cast<EFI_GET_MEMORY_MAP>(gST->BootServices->GetMemoryMap);
    auto freePages = reinterpret_cast<EFI_FREE_PAGES>(gST->BootServices->FreePages);
    if (!allocatePages || !getMemoryMap) return EFI_NOT_FOUND;

    // A temporary 128 KiB map buffer is enough for the firmware maps used by
    // VMware/QEMU and typical PCs.  If firmware reports a larger map we fall
    // back to AllocateMaxAddress below.
    constexpr size_t MAP_PAGES = 32;
    EFI_PHYSICAL_ADDRESS mapAddr = 0;
    EFI_STATUS st = allocatePages(AllocateAnyPages, EfiLoaderData, MAP_PAGES, &mapAddr);
    if (EFI_ERROR(st)) return st;
    size_t mapSize = MAP_PAGES * 4096ULL, mapKey = 0, descSize = 0; uint32_t descVersion = 0;
    st = getMemoryMap(&mapSize, reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(static_cast<uintptr_t>(mapAddr)), &mapKey, &descSize, &descVersion);
    if (!EFI_ERROR(st) && descSize >= sizeof(EFI_MEMORY_DESCRIPTOR)) {
        uint64_t best = 0;
        auto* raw = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(mapAddr));
        for (size_t off = 0; off + descSize <= mapSize; off += descSize) {
            auto* d = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(raw + off);
            if (d->Type != EfiConventionalMemory || d->NumberOfPages < pages) continue;
            uint64_t start = d->PhysicalStart;
            uint64_t end = start + d->NumberOfPages * 4096ULL;
            if (end > 0x100000000ULL) end = 0x100000000ULL;
            const uint64_t need = static_cast<uint64_t>(pages) * 4096ULL;
            if (end <= start || end - start < need) continue;
            uint64_t candidate = (end - need) & ~0x1FFFFFULL; // prefer 2 MiB alignment
            if (candidate < start) candidate = (end - need) & ~4095ULL;
            if (candidate >= start && candidate > best) best = candidate;
        }
        if (best) {
            EFI_PHYSICAL_ADDRESS requested = best;
            st = allocatePages(AllocateAddress, EfiLoaderData, pages, &requested);
            if (!EFI_ERROR(st)) base = requested;
        }
    }
    if (freePages) freePages(mapAddr, MAP_PAGES);
    if (base) return EFI_SUCCESS;
    base = 0xFFFFFFFFULL;
    return allocatePages(AllocateMaxAddress, EfiLoaderData, pages, &base);
}
static EFI_STATUS load_elf_kernel(FsContext& fs, const CHAR16* configuredPath, uint32_t expectedCrc,
                                  uint64_t& loadBase, uint64_t& entryOut, uint64_t& fileSizeOut,
                                  uint64_t& memorySizeOut, uint32_t& actualCrcOut) {
    loadBase = entryOut = fileSizeOut = memorySizeOut = 0; actualCrcOut = 0;
    CHAR16 preferred[256];
    if (configuredPath) { size_t i=0; for (; configuredPath[i] && i+1<256; ++i) preferred[i]=configuredPath[i]; preferred[i]=0; }
    else ascii_to_char16("\\SYSTEM\\KERNEL.ELF", preferred, 256);
    EFI_FILE_PROTOCOL* f = nullptr;
    EFI_STATUS st = open_read_candidate(fs, preferred, "\\SYSTEM\\KERNEL.ELF", "\\KERNEL.ELF", &f);
    if (EFI_ERROR(st) || !f) return st;

    uint64_t fileBytes = 0;
    st = get_file_size_robust(f, fileBytes);
    if (EFI_ERROR(st) || fileBytes < sizeof(Elf64Ehdr) || fileBytes > 8ULL*1024*1024) { f->Close(f); return EFI_NOT_FOUND; }

    auto allocatePages = reinterpret_cast<EFI_ALLOCATE_PAGES>(gST->BootServices->AllocatePages);
    auto freePages = reinterpret_cast<EFI_FREE_PAGES>(gST->BootServices->FreePages);
    const size_t filePages = static_cast<size_t>((fileBytes + 4095ULL)/4096ULL);
    EFI_PHYSICAL_ADDRESS fileAddr = 0;
    st = allocatePages(AllocateAnyPages, EfiLoaderData, filePages, &fileAddr);
    if (EFI_ERROR(st)) { f->Close(f); return st; }
    memset(reinterpret_cast<void*>(static_cast<uintptr_t>(fileAddr)),0,filePages*4096ULL);
    f->SetPosition(f,0);
    st = read_exact_chunks(f,reinterpret_cast<void*>(static_cast<uintptr_t>(fileAddr)),fileBytes);
    f->Close(f);
    if (EFI_ERROR(st)) { if(freePages)freePages(fileAddr,filePages); return st; }

    auto* image = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(fileAddr));
    actualCrcOut = crc32_bytes(image, fileBytes);
    if (expectedCrc && actualCrcOut != expectedCrc) {
        print(L"ERROR: kernel integrity check failed.\r\n");
        if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND;
    }
    auto* eh = reinterpret_cast<const Elf64Ehdr*>(image);
    if (!elf_header_ok(eh,fileBytes)) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
    auto* ph = reinterpret_cast<const Elf64Phdr*>(image + eh->phoff);
    uint64_t minV = ~0ULL, maxV = 0; bool entryCovered = false; uint16_t loadCount = 0;
    for (uint16_t i=0;i<eh->phnum;++i) {
        if (ph[i].type != ELF_PT_LOAD) continue;
        ++loadCount;
        if (ph[i].filesz > ph[i].memsz || ph[i].offset > fileBytes || ph[i].filesz > fileBytes - ph[i].offset) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
        if (ph[i].align && (!is_pow2_64(ph[i].align) || ph[i].align > 0x200000ULL)) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
        if (ph[i].memsz && ph[i].vaddr > ~0ULL - ph[i].memsz) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
        if (((ph[i].vaddr - ph[i].offset) & 0xFFFULL) != 0) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
        const uint64_t segEnd = ph[i].vaddr + ph[i].memsz;
        uint64_t lo = ph[i].vaddr & ~4095ULL;
        uint64_t hi = (segEnd + 4095ULL) & ~4095ULL;
        if (lo < minV) minV = lo; if (hi > maxV) maxV = hi;
        if ((ph[i].flags & 1u) && eh->entry >= ph[i].vaddr && eh->entry < segEnd) entryCovered = true;
        for (uint16_t j=0;j<i;++j) if (ph[j].type==ELF_PT_LOAD && ph[j].memsz) {
            const uint64_t a0=ph[i].vaddr,a1=segEnd,b0=ph[j].vaddr,b1=ph[j].vaddr+ph[j].memsz;
            if (a0 < b1 && b0 < a1) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
        }
    }
    if (!loadCount || !entryCovered || minV == ~0ULL || maxV <= minV || minV != 0) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
    const uint64_t span = maxV - minV;
    if (span > 64ULL*1024*1024) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
    const size_t pages = static_cast<size_t>((span + 4095ULL)/4096ULL);
    // Choose the load region from the current UEFI memory map instead of using
    // a fixed address.  The fallback still keeps it below 4 GiB.
    EFI_PHYSICAL_ADDRESS base = 0;
    st = allocate_kernel_region_from_memory_map(pages, base);
    if (EFI_ERROR(st)) { if(freePages)freePages(fileAddr,filePages); return st; }
    memset(reinterpret_cast<void*>(static_cast<uintptr_t>(base)),0,pages*4096ULL);

    for (uint16_t i=0;i<eh->phnum;++i) {
        if (ph[i].type != ELF_PT_LOAD) continue;
        void* dst = reinterpret_cast<void*>(static_cast<uintptr_t>(base + ph[i].vaddr));
        memcpy(dst,image + ph[i].offset,static_cast<size_t>(ph[i].filesz));
    }

    // Static PIE currently needs only R_X86_64_RELATIVE relocations. Apply every RELA section.
    if (eh->shoff && eh->shnum && eh->shentsize == sizeof(Elf64Shdr) &&
        eh->shoff + uint64_t(eh->shnum)*sizeof(Elf64Shdr) <= fileBytes) {
        auto* sh = reinterpret_cast<const Elf64Shdr*>(image + eh->shoff);
        for (uint16_t i=0;i<eh->shnum;++i) {
            if (sh[i].type != ELF_SHT_RELA || !sh[i].entsize) continue;
            if (sh[i].offset + sh[i].size > fileBytes || sh[i].entsize != sizeof(Elf64Rela)) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
            const uint64_t count = sh[i].size / sizeof(Elf64Rela);
            auto* rela = reinterpret_cast<const Elf64Rela*>(image + sh[i].offset);
            for (uint64_t r=0;r<count;++r) {
                const uint32_t type = static_cast<uint32_t>(rela[r].info & 0xFFFFFFFFu);
                if (type != ELF_R_X86_64_RELATIVE || rela[r].offset + 8 > span) { if(freePages)freePages(fileAddr,filePages); return EFI_NOT_FOUND; }
                auto* where = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(base + rela[r].offset));
                *where = base + static_cast<uint64_t>(rela[r].addend);
            }
        }
    }

    loadBase = base;
    entryOut = base + eh->entry;
    fileSizeOut = fileBytes;
    memorySizeOut = span;
    if (freePages) freePages(fileAddr,filePages);
    return EFI_SUCCESS;
}

static EFI_STATUS load_blob(FsContext& fs, CHAR16* path, uint64_t& baseOut, uint64_t& sizeOut) {
    baseOut = 0;
    sizeOut = 0;
    EFI_FILE_PROTOCOL* f = nullptr;
    EFI_STATUS s = open_read_candidate(fs, path, "\\SYSTEM\\ROOTFS.IMG", "\\ROOTFS.IMG", &f);
    if (EFI_ERROR(s) || !f) {
        print_status(L"Rootfs open status", s);
        return s;
    }
    s = get_file_size_robust(f, sizeOut);
    if (EFI_ERROR(s)) {
        print_status(L"Rootfs size status", s);
        f->Close(f);
        return s;
    }

    const size_t pages = static_cast<size_t>((sizeOut + 4095ULL) / 4096ULL);
    EFI_PHYSICAL_ADDRESS addr = 0;
    auto allocatePages = reinterpret_cast<EFI_ALLOCATE_PAGES>(gST->BootServices->AllocatePages);
    s = allocatePages(AllocateAnyPages, EfiLoaderData, pages, &addr);
    if (EFI_ERROR(s)) {
        print_status(L"Rootfs AllocatePages status", s);
        f->Close(f);
        return s;
    }
    memset(reinterpret_cast<void*>(static_cast<uintptr_t>(addr)), 0, pages * 4096ULL);
    f->SetPosition(f, 0);
    s = read_exact_chunks(f, reinterpret_cast<void*>(static_cast<uintptr_t>(addr)), sizeOut);
    f->Close(f);
    if (EFI_ERROR(s)) {
        print_status(L"Rootfs read status", s);
        return s;
    }
    baseOut = addr;
    return EFI_SUCCESS;
}

[[noreturn]] static void handoff_to_kernel(uint64_t kernelBase, ZeroNTBootInfo* bootInfo, uint64_t stackTop) {
    // UEFI owns the stack used by efi_main. After ExitBootServices the kernel must
    // not depend on that firmware stack. Switch to a dedicated loader-allocated
    // stack below 4 GiB, then enter the SysV ABI kernel with BootInfo in RDI.
    __asm__ __volatile__(
        "cli\n\t"
        "movq %0, %%rsp\n\t"
        "andq $-16, %%rsp\n\t"
        "xorq %%rbp, %%rbp\n\t"
        "movq %1, %%rdi\n\t"
        "call *%2\n\t"
        "1: hlt\n\t"
        "jmp 1b\n\t"
        :
        : "r"(stackTop), "r"(bootInfo), "r"(kernelBase)
        : "memory", "rdi"
    );
    __builtin_unreachable();
}

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* systemTable) {
    gST = systemTable;
    if (!gST || !gST->BootServices) return 1;
    if (gST->ConOut && gST->ConOut->ClearScreen) gST->ConOut->ClearScreen(gST->ConOut);

    FsContext fs{};
    if (!open_root(imageHandle, fs)) {
        print(L"ERROR: Could not open EFI system volume.\r\n");
        return 1;
    }

    CHAR16 kernelPath[256];
    CHAR16 rootfsPath[256];
    load_config_path(fs, "kernel", "\\SYSTEM\\KERNEL.ELF", kernelPath, 256);
    load_config_path(fs, "rootfs", "\\SYSTEM\\ROOTFS.IMG", rootfsPath, 256);
    char bootMode[32]{}; load_config_ascii(fs, "mode", "normal", bootMode, sizeof(bootMode));
    const bool safeMode = ascii_ieq(bootMode, "safe");
    const bool recoveryMode = ascii_ieq(bootMode, "recovery");
    const uint32_t expectedKernelCrc = load_config_hex32(fs, "kernel_crc32");
    const uint32_t expectedRootfsCrc = load_config_hex32(fs, "rootfs_crc32");
    char registerBoot[16]{}; load_config_ascii(fs,"register_boot_entry","0",registerBoot,sizeof(registerBoot));
    if(ascii_ieq(registerBoot,"1")||ascii_ieq(registerBoot,"yes")||ascii_ieq(registerBoot,"true")){
        if(register_zeront_boot_entry(imageHandle))print(L"UEFI: zeroNT boot entry registered.\r\n");
        else print(L"UEFI: boot entry registration unavailable; fallback path remains valid.\r\n");
    }

    uint64_t kernelBase = 0;
    uint64_t kernelEntry = 0;
    uint64_t kernelSize = 0;
    uint64_t kernelMemorySize = 0;
    uint32_t kernelCrc = 0;
    EFI_STATUS s = load_elf_kernel(fs, kernelPath, expectedKernelCrc, kernelBase, kernelEntry, kernelSize, kernelMemorySize, kernelCrc);
    if (EFI_ERROR(s)) {
        print(L"ERROR: kernel load failed. See status above.\r\n");
        return s;
    }

    uint64_t rootfsBase = 0;
    uint64_t rootfsSize = 0;
    s = load_blob(fs, rootfsPath, rootfsBase, rootfsSize);
    if (EFI_ERROR(s)) {
        print(L"ERROR: rootfs load failed. See status above.\r\n");
        return s;
    }
    const uint32_t rootfsCrc = crc32_bytes(reinterpret_cast<void*>(static_cast<uintptr_t>(rootfsBase)), rootfsSize);
    if (expectedRootfsCrc && rootfsCrc != expectedRootfsCrc) {
        print(L"ERROR: rootfs integrity check failed.\r\n");
        return EFI_NOT_FOUND;
    }

    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = nullptr;
    s = gST->BootServices->LocateProtocol(&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, nullptr, reinterpret_cast<void**>(&gop));
    if (EFI_ERROR(s) || !gop || !gop->Mode || !gop->Mode->Info) {
        print(L"ERROR: GOP framebuffer not available.\r\n");
        return s;
    }
    if (!choose_graphics_mode(fs, gop)) {
        print(L"WARNING: requested GOP mode failed; continuing with firmware mode.\r\n");
    }
    if (!gop->Mode || !gop->Mode->Info || !gop->Mode->FrameBufferBase) {
        print(L"ERROR: GOP framebuffer invalid after mode selection.\r\n");
        return 1;
    }
    print(L"VIDEO: ");
    print_u32(gop->Mode->Info->HorizontalResolution);
    print(L"x");
    print_u32(gop->Mode->Info->VerticalResolution);
    print(L"\r\n");

    auto allocatePages = reinterpret_cast<EFI_ALLOCATE_PAGES>(gST->BootServices->AllocatePages);
    auto getMemoryMap = reinterpret_cast<EFI_GET_MEMORY_MAP>(gST->BootServices->GetMemoryMap);
    auto exitBootServices = reinterpret_cast<EFI_EXIT_BOOT_SERVICES>(gST->BootServices->ExitBootServices);

    EFI_PHYSICAL_ADDRESS bootInfoAddr = 0;
    s = allocatePages(AllocateAnyPages, EfiLoaderData, 1, &bootInfoAddr);
    if (EFI_ERROR(s)) return s;
    auto* bi = reinterpret_cast<ZeroNTBootInfo*>(static_cast<uintptr_t>(bootInfoAddr));
    memset(bi, 0, 4096);

    size_t mapSize = 0, mapKey = 0, descSize = 0;
    uint32_t descVersion = 0;
    s = getMemoryMap(&mapSize, nullptr, &mapKey, &descSize, &descVersion);
    if (s != EFI_BUFFER_TOO_SMALL && EFI_ERROR(s)) return s;
    mapSize += descSize * 16;
    const size_t mapPages = (mapSize + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS mapAddr = 0;
    s = allocatePages(AllocateAnyPages, EfiLoaderData, mapPages, &mapAddr);
    if (EFI_ERROR(s)) return s;

    // Dedicated post-UEFI kernel stack. Keep it below 4 GiB so the kernel's
    // early 4 GiB identity map always preserves it after CR3 is replaced.
    static constexpr size_t KERNEL_STACK_PAGES = 32; // 128 KiB
    EFI_PHYSICAL_ADDRESS kernelStackAddr = 0xFFFFFFFFULL;
    s = allocatePages(AllocateMaxAddress, EfiLoaderData, KERNEL_STACK_PAGES, &kernelStackAddr);
    if (EFI_ERROR(s)) {
        print_status(L"Kernel stack AllocatePages status", s);
        return s;
    }
    memset(reinterpret_cast<void*>(static_cast<uintptr_t>(kernelStackAddr)), 0, KERNEL_STACK_PAGES * 4096ULL);
    const uint64_t kernelStackTop = static_cast<uint64_t>(kernelStackAddr) + KERNEL_STACK_PAGES * 4096ULL;

    bi->magic = ZERONT_BOOTINFO_MAGIC;
    bi->version = ZERONT_BOOTINFO_VERSION;
    bi->framebuffer = gop->Mode->FrameBufferBase;
    bi->framebufferSize = gop->Mode->FrameBufferSize;
    bi->width = gop->Mode->Info->HorizontalResolution;
    bi->height = gop->Mode->Info->VerticalResolution;
    bi->pixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;
    bi->pixelFormat = static_cast<uint32_t>(gop->Mode->Info->PixelFormat);
    bi->memoryMap = mapAddr;
    bi->acpiRsdp = reinterpret_cast<uint64_t>(find_acpi_rsdp(gST));
    bi->kernelBase = kernelBase;
    bi->kernelSize = kernelSize;
    bi->kernelMemorySize = kernelMemorySize;
    bi->rootfsBase = rootfsBase;
    bi->rootfsSize = rootfsSize;
    bi->rootfsSectorSize = 512;
    bi->bootFlags = (safeMode ? ZERONT_BOOT_SAFE_MODE : 0u) |
                    (recoveryMode ? ZERONT_BOOT_RECOVERY_MODE : 0u) |
                    (expectedKernelCrc ? ZERONT_BOOT_KERNEL_CRC_OK : 0u) |
                    (expectedRootfsCrc ? ZERONT_BOOT_ROOTFS_CRC_OK : 0u);
    bi->kernelCrc32 = kernelCrc;
    bi->rootfsCrc32 = rootfsCrc;

    mapSize = mapPages * 4096;
    s = getMemoryMap(&mapSize, reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(static_cast<uintptr_t>(mapAddr)),
                     &mapKey, &descSize, &descVersion);
    if (EFI_ERROR(s)) return s;
    bi->memoryMapSize = mapSize;
    bi->memoryDescriptorSize = descSize;
    bi->memoryDescriptorVersion = descVersion;

    s = exitBootServices(imageHandle, mapKey);
    if (EFI_ERROR(s)) {
        mapSize = mapPages * 4096;
        s = getMemoryMap(&mapSize, reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(static_cast<uintptr_t>(mapAddr)),
                         &mapKey, &descSize, &descVersion);
        if (EFI_ERROR(s)) return s;
        bi->memoryMapSize = mapSize;
        bi->memoryDescriptorSize = descSize;
        bi->memoryDescriptorVersion = descVersion;
        s = exitBootServices(imageHandle, mapKey);
        if (EFI_ERROR(s)) return s;
    }

    handoff_to_kernel(kernelEntry, bi, kernelStackTop);
}
