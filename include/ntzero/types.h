#ifndef NTZERO_TYPES_H
#define NTZERO_TYPES_H

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         i8;
typedef signed short        i16;
typedef signed int          i32;
typedef signed long long    i64;
typedef u64                 usize;
typedef i64                 isize;
typedef u64                 NTSTATUS;
typedef u64                 NTZ_HANDLE;

#define NTZ_TRUE  1u
#define NTZ_FALSE 0u

#define STATUS_SUCCESS               ((NTSTATUS)0x0000000000000000ULL)
#define STATUS_PENDING               ((NTSTATUS)0x0000000000000103ULL)
#define STATUS_INVALID_PARAMETER     ((NTSTATUS)0xC000000DULL)
#define STATUS_NO_MEMORY             ((NTSTATUS)0xC0000017ULL)
#define STATUS_OBJECT_NAME_COLLISION ((NTSTATUS)0xC0000035ULL)
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034ULL)
#define STATUS_PORT_DISCONNECTED     ((NTSTATUS)0xC0000037ULL)
#define STATUS_NOT_FOUND             ((NTSTATUS)0xC0000225ULL)
#define STATUS_BUFFER_TOO_SMALL      ((NTSTATUS)0xC0000023ULL)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009AULL)

#endif
