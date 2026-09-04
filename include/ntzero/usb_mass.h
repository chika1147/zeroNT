#ifndef NTZERO_USB_MASS_H
#define NTZERO_USB_MASS_H

#include "types.h"

typedef struct __attribute__((packed)) NTZ_USB_MASS_CBW {
    u32 signature;
    u32 tag;
    u32 transfer_length;
    u8 flags;
    u8 lun;
    u8 cdb_length;
    u8 cdb[16];
} NTZ_USB_MASS_CBW;

typedef struct __attribute__((packed)) NTZ_USB_MASS_CSW {
    u32 signature;
    u32 tag;
    u32 residue;
    u8 status;
} NTZ_USB_MASS_CSW;

void UsbMassBuildInquiry(NTZ_USB_MASS_CBW *cbw, u32 tag, u8 allocation_length);
void UsbMassBuildReadCapacity10(NTZ_USB_MASS_CBW *cbw, u32 tag);
void UsbMassBuildRead10(NTZ_USB_MASS_CBW *cbw, u32 tag, u32 lba, u16 blocks, u32 byte_count);
void UsbMassBuildWrite10(NTZ_USB_MASS_CBW *cbw, u32 tag, u32 lba, u16 blocks, u32 byte_count);
int UsbMassValidateCsw(const NTZ_USB_MASS_CSW *csw, u32 expected_tag);

#endif
