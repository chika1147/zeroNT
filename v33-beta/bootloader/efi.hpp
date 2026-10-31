#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint64_t EFI_STATUS;
typedef void* EFI_HANDLE;
typedef void* EFI_EVENT;
typedef uint64_t EFI_TPL;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;
typedef uint16_t CHAR16;
typedef uint8_t BOOLEAN;

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_ERROR(x) (((x) & 0x8000000000000000ULL) != 0)

struct EFI_GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
};

struct EFI_TABLE_HEADER {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
};

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_RUNTIME_SERVICES;
struct EFI_BOOT_SERVICES;

struct EFI_INPUT_KEY {
    uint16_t ScanCode;
    CHAR16 UnicodeChar;
};

typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This,
    BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This,
    EFI_INPUT_KEY* Key
);

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    EFI_EVENT WaitForKey;
};

struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;

struct EFI_KEY_STATE {
    uint32_t KeyShiftState;
    uint8_t KeyToggleState;
};

struct EFI_KEY_DATA {
    EFI_INPUT_KEY Key;
    EFI_KEY_STATE KeyState;
};

typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET_EX)(
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL* This,
    BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY_EX)(
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL* This,
    EFI_KEY_DATA* KeyData
);

typedef EFI_STATUS (EFIAPI *EFI_SET_STATE_EX)(
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL* This,
    uint8_t* KeyToggleState
);

typedef EFI_STATUS (EFIAPI *EFI_REGISTER_KEY_NOTIFY)(
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL* This,
    EFI_KEY_DATA* KeyData,
    void* KeyNotificationFunction,
    void** NotifyHandle
);

typedef EFI_STATUS (EFIAPI *EFI_UNREGISTER_KEY_NOTIFY)(
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL* This,
    void* NotificationHandle
);

struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL {
    EFI_INPUT_RESET_EX Reset;
    EFI_INPUT_READ_KEY_EX ReadKeyStrokeEx;
    EFI_EVENT WaitForKeyEx;
    EFI_SET_STATE_EX SetState;
    EFI_REGISTER_KEY_NOTIFY RegisterKeyNotify;
    EFI_UNREGISTER_KEY_NOTIFY UnregisterKeyNotify;
};

static EFI_GUID EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID = {
    0xdd9e7534, 0x7762, 0x4698,
    {0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa}
};

static constexpr uint32_t EFI_RIGHT_SHIFT_PRESSED   = 0x00000001;
static constexpr uint32_t EFI_LEFT_SHIFT_PRESSED    = 0x00000002;
static constexpr uint32_t EFI_RIGHT_CONTROL_PRESSED = 0x00000004;
static constexpr uint32_t EFI_LEFT_CONTROL_PRESSED  = 0x00000008;
static constexpr uint32_t EFI_RIGHT_ALT_PRESSED     = 0x00000010;
static constexpr uint32_t EFI_LEFT_ALT_PRESSED      = 0x00000020;
static constexpr uint32_t EFI_SHIFT_STATE_VALID     = 0x80000000;

static constexpr uint8_t EFI_SCROLL_LOCK_ACTIVE = 0x01;
static constexpr uint8_t EFI_NUM_LOCK_ACTIVE    = 0x02;
static constexpr uint8_t EFI_CAPS_LOCK_ACTIVE   = 0x04;
static constexpr uint8_t EFI_TOGGLE_STATE_VALID = 0x80;

static constexpr uint16_t EFI_SCAN_UP        = 0x01;
static constexpr uint16_t EFI_SCAN_DOWN      = 0x02;
static constexpr uint16_t EFI_SCAN_RIGHT     = 0x03;
static constexpr uint16_t EFI_SCAN_LEFT      = 0x04;
static constexpr uint16_t EFI_SCAN_HOME      = 0x05;
static constexpr uint16_t EFI_SCAN_END       = 0x06;
static constexpr uint16_t EFI_SCAN_INSERT    = 0x07;
static constexpr uint16_t EFI_SCAN_DELETE    = 0x08;
static constexpr uint16_t EFI_SCAN_PAGE_UP   = 0x09;
static constexpr uint16_t EFI_SCAN_PAGE_DOWN = 0x0A;
static constexpr uint16_t EFI_SCAN_F1        = 0x0B;
static constexpr uint16_t EFI_SCAN_F2        = 0x0C;
static constexpr uint16_t EFI_SCAN_F3        = 0x0D;
static constexpr uint16_t EFI_SCAN_F4        = 0x0E;
static constexpr uint16_t EFI_SCAN_F5        = 0x0F;
static constexpr uint16_t EFI_SCAN_ESC       = 0x17;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
    BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
    const CHAR16* String
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_TEST_STRING)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
    const CHAR16* String
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_QUERY_MODE)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
    size_t ModeNumber,
    size_t* Columns,
    size_t* Rows
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_MODE)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
    size_t ModeNumber
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
    size_t Attribute
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
    size_t Column,
    size_t Row
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_ENABLE_CURSOR)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
    BOOLEAN Visible
);

struct EFI_SIMPLE_TEXT_OUTPUT_MODE {
    int32_t MaxMode;
    int32_t Mode;
    int32_t Attribute;
    int32_t CursorColumn;
    int32_t CursorRow;
    BOOLEAN CursorVisible;
};

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET Reset;
    EFI_TEXT_STRING OutputString;
    EFI_TEXT_TEST_STRING TestString;
    EFI_TEXT_QUERY_MODE QueryMode;
    EFI_TEXT_SET_MODE SetMode;
    EFI_TEXT_SET_ATTRIBUTE SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION SetCursorPosition;
    EFI_TEXT_ENABLE_CURSOR EnableCursor;
    EFI_SIMPLE_TEXT_OUTPUT_MODE* Mode;
};

struct EFI_CONFIGURATION_TABLE {
    EFI_GUID VendorGuid;
    void* VendorTable;
};

struct EFI_TIME {
    uint16_t Year;
    uint8_t Month;
    uint8_t Day;
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
    uint8_t Pad1;
    uint32_t Nanosecond;
    int16_t TimeZone;
    uint8_t Daylight;
    uint8_t Pad2;
};

struct EFI_TIME_CAPABILITIES {
    uint32_t Resolution;
    uint32_t Accuracy;
    BOOLEAN SetsToZero;
};

typedef EFI_STATUS (EFIAPI *EFI_GET_TIME)(
    EFI_TIME* Time,
    EFI_TIME_CAPABILITIES* Capabilities
);

enum EFI_RESET_TYPE {
    EfiResetCold = 0,
    EfiResetWarm = 1,
    EfiResetShutdown = 2,
    EfiResetPlatformSpecific = 3
};

typedef void (EFIAPI *EFI_RESET_SYSTEM)(
    EFI_RESET_TYPE ResetType,
    EFI_STATUS ResetStatus,
    size_t DataSize,
    void* ResetData
);

typedef EFI_STATUS (EFIAPI *EFI_GET_VARIABLE)(
    CHAR16* VariableName,
    EFI_GUID* VendorGuid,
    uint32_t* Attributes,
    size_t* DataSize,
    void* Data
);

typedef EFI_STATUS (EFIAPI *EFI_SET_VARIABLE)(
    CHAR16* VariableName,
    EFI_GUID* VendorGuid,
    uint32_t Attributes,
    size_t DataSize,
    void* Data
);

struct EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER Hdr;
    EFI_GET_TIME GetTime;
    void* SetTime;
    void* GetWakeupTime;
    void* SetWakeupTime;
    void* SetVirtualAddressMap;
    void* ConvertPointer;
    EFI_GET_VARIABLE GetVariable;
    void* GetNextVariableName;
    EFI_SET_VARIABLE SetVariable;
    void* GetNextHighMonotonicCount;
    EFI_RESET_SYSTEM ResetSystem;
    void* UpdateCapsule;
    void* QueryCapsuleCapabilities;
    void* QueryVariableInfo;
};

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    EFI_GUID* Protocol,
    void* Registration,
    void** Interface
);

typedef EFI_STATUS (EFIAPI *EFI_STALL)(size_t Microseconds);

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;

    void* RaiseTPL;
    void* RestoreTPL;

    void* AllocatePages;
    void* FreePages;
    void* GetMemoryMap;
    void* AllocatePool;
    void* FreePool;

    void* CreateEvent;
    void* SetTimer;
    void* WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;

    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    void* HandleProtocol;
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;

    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    void* ExitBootServices;

    void* GetNextMonotonicCount;
    EFI_STALL Stall;
    void* SetWatchdogTimer;

    void* ConnectController;
    void* DisconnectController;

    void* OpenProtocol;
    void* CloseProtocol;
    void* OpenProtocolInformation;

    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;

    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;

    void* CalculateCrc32;
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
};

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16* FirmwareVendor;
    uint32_t FirmwareRevision;

    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn;

    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;

    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;

    EFI_RUNTIME_SERVICES* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;

    size_t NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE* ConfigurationTable;
};


// UEFI device path + file-system protocols used by the loader.
struct EFI_DEVICE_PATH_PROTOCOL {
    uint8_t Type;
    uint8_t SubType;
    uint8_t Length[2];
};
struct EFI_FILE_PROTOCOL;
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE DeviceHandle;
    void* FilePath;
    void* Reserved;
    uint32_t LoadOptionsSize;
    void* LoadOptions;
    void* ImageBase;
    uint64_t ImageSize;
    uint32_t ImageCodeType;
    uint32_t ImageDataType;
    void* Unload;
};

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_OPEN_VOLUME)(
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* This,
    EFI_FILE_PROTOCOL** Root
);

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_SIMPLE_FILE_SYSTEM_OPEN_VOLUME OpenVolume;
};

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    EFI_FILE_PROTOCOL* This,
    EFI_FILE_PROTOCOL** NewHandle,
    CHAR16* FileName,
    uint64_t OpenMode,
    uint64_t Attributes
);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL* This);
typedef EFI_STATUS (EFIAPI *EFI_FILE_DELETE)(EFI_FILE_PROTOCOL* This);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL* This, size_t* BufferSize, void* Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_WRITE)(EFI_FILE_PROTOCOL* This, size_t* BufferSize, void* Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_POSITION)(EFI_FILE_PROTOCOL* This, uint64_t* Position);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(EFI_FILE_PROTOCOL* This, uint64_t Position);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL* This, EFI_GUID* InformationType, size_t* BufferSize, void* Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_INFO)(EFI_FILE_PROTOCOL* This, EFI_GUID* InformationType, size_t BufferSize, void* Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_FLUSH)(EFI_FILE_PROTOCOL* This);

struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    EFI_FILE_DELETE Delete;
    EFI_FILE_READ Read;
    EFI_FILE_WRITE Write;
    EFI_FILE_GET_POSITION GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    EFI_FILE_SET_INFO SetInfo;
    EFI_FILE_FLUSH Flush;
    void* OpenEx;
    void* ReadEx;
    void* WriteEx;
    void* FlushEx;
};

struct EFI_FILE_INFO {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    uint64_t Attribute;
    CHAR16 FileName[1];
};

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE Handle, EFI_GUID* Protocol, void** Interface);

static EFI_GUID EFI_DEVICE_PATH_PROTOCOL_GUID = {
    0x09576e91, 0x6d3f, 0x11d2,
    {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}
};
static EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5b1b31a1, 0x9562, 0x11d2,
    {0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}
};
static EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x964e5b22, 0x6459, 0x11d2,
    {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}
};
static EFI_GUID EFI_FILE_INFO_GUID = {
    0x09576e92, 0x6d3f, 0x11d2,
    {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}
};

static constexpr uint64_t EFI_FILE_MODE_READ   = 0x0000000000000001ULL;
static constexpr uint64_t EFI_FILE_MODE_WRITE  = 0x0000000000000002ULL;
static constexpr uint64_t EFI_FILE_MODE_CREATE = 0x8000000000000000ULL;
static constexpr uint64_t EFI_FILE_DIRECTORY   = 0x0000000000000010ULL;

enum EFI_GRAPHICS_PIXEL_FORMAT {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3,
    PixelFormatMax = 4
};

struct EFI_PIXEL_BITMASK {
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
};

struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    uint32_t PixelsPerScanLine;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    size_t SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    size_t FrameBufferSize;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL* This,
    uint32_t ModeNumber,
    size_t* SizeOfInfo,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** Info
);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL* This,
    uint32_t ModeNumber
);

struct EFI_GRAPHICS_OUTPUT_BLT_PIXEL {
    uint8_t Blue;
    uint8_t Green;
    uint8_t Red;
    uint8_t Reserved;
};

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL* This,
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL* BltBuffer,
    uint32_t BltOperation,
    size_t SourceX,
    size_t SourceY,
    size_t DestinationX,
    size_t DestinationY,
    size_t Width,
    size_t Height,
    size_t Delta
);

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE SetMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};

struct EFI_SIMPLE_POINTER_PROTOCOL;

struct EFI_SIMPLE_POINTER_STATE {
    int32_t RelativeMovementX;
    int32_t RelativeMovementY;
    int32_t RelativeMovementZ;
    BOOLEAN LeftButton;
    BOOLEAN RightButton;
};

struct EFI_SIMPLE_POINTER_MODE {
    uint64_t ResolutionX;
    uint64_t ResolutionY;
    uint64_t ResolutionZ;
    BOOLEAN LeftButton;
    BOOLEAN RightButton;
};

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_POINTER_RESET)(
    EFI_SIMPLE_POINTER_PROTOCOL* This,
    BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_POINTER_GET_STATE)(
    EFI_SIMPLE_POINTER_PROTOCOL* This,
    EFI_SIMPLE_POINTER_STATE* State
);

struct EFI_SIMPLE_POINTER_PROTOCOL {
    EFI_SIMPLE_POINTER_RESET Reset;
    EFI_SIMPLE_POINTER_GET_STATE GetState;
    EFI_EVENT WaitForInput;
    EFI_SIMPLE_POINTER_MODE* Mode;
};

struct EFI_ABSOLUTE_POINTER_PROTOCOL;

struct EFI_ABSOLUTE_POINTER_STATE {
    uint64_t CurrentX;
    uint64_t CurrentY;
    uint64_t CurrentZ;
    uint32_t ActiveButtons;
};

struct EFI_ABSOLUTE_POINTER_MODE {
    uint64_t AbsoluteMinX;
    uint64_t AbsoluteMinY;
    uint64_t AbsoluteMinZ;
    uint64_t AbsoluteMaxX;
    uint64_t AbsoluteMaxY;
    uint64_t AbsoluteMaxZ;
    uint32_t Attributes;
};

typedef EFI_STATUS (EFIAPI *EFI_ABSOLUTE_POINTER_RESET)(
    EFI_ABSOLUTE_POINTER_PROTOCOL* This,
    BOOLEAN ExtendedVerification
);

typedef EFI_STATUS (EFIAPI *EFI_ABSOLUTE_POINTER_GET_STATE)(
    EFI_ABSOLUTE_POINTER_PROTOCOL* This,
    EFI_ABSOLUTE_POINTER_STATE* State
);

struct EFI_ABSOLUTE_POINTER_PROTOCOL {
    EFI_ABSOLUTE_POINTER_RESET Reset;
    EFI_ABSOLUTE_POINTER_GET_STATE GetState;
    EFI_EVENT WaitForInput;
    EFI_ABSOLUTE_POINTER_MODE* Mode;
};

static constexpr uint32_t EFI_ABSP_TOUCH_ACTIVE = 0x00000001u;
static constexpr uint32_t EFI_ABSP_ALT_ACTIVE   = 0x00000002u;

static EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042a9de, 0x23dc, 0x4a38,
    {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

static EFI_GUID EFI_SIMPLE_POINTER_PROTOCOL_GUID = {
    0x31878c87, 0x0b75, 0x11d5,
    {0x9a, 0x4f, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}
};


static EFI_GUID EFI_ABSOLUTE_POINTER_PROTOCOL_GUID = {
    0x8d59d32b, 0xc655, 0x4ae9,
    {0x9b, 0x15, 0xf2, 0x59, 0x04, 0x99, 0x2a, 0x43}
};
