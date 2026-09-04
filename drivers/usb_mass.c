#include "ntzero/usb_mass.h"

#define CBW_SIGNATURE 0x43425355u
#define CSW_SIGNATURE 0x53425355u

static void zero_cbw(NTZ_USB_MASS_CBW *cbw) {
    u8 *p = (u8 *)cbw;
    usize i;
    for (i = 0u; i < sizeof(*cbw); ++i) p[i] = 0u;
}

static void store_be16(u8 *p, u16 value) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}

static void store_be32(u8 *p, u32 value) {
    p[0] = (u8)(value >> 24);
    p[1] = (u8)(value >> 16);
    p[2] = (u8)(value >> 8);
    p[3] = (u8)value;
}

void UsbMassBuildInquiry(NTZ_USB_MASS_CBW *cbw, u32 tag, u8 allocation_length) {
    if (cbw == (NTZ_USB_MASS_CBW *)0) return;
    zero_cbw(cbw);
    cbw->signature = CBW_SIGNATURE;
    cbw->tag = tag;
    cbw->transfer_length = allocation_length;
    cbw->flags = 0x80u;
    cbw->cdb_length = 6u;
    cbw->cdb[0] = 0x12u;
    cbw->cdb[4] = allocation_length;
}

void UsbMassBuildReadCapacity10(NTZ_USB_MASS_CBW *cbw, u32 tag) {
    if (cbw == (NTZ_USB_MASS_CBW *)0) return;
    zero_cbw(cbw);
    cbw->signature = CBW_SIGNATURE;
    cbw->tag = tag;
    cbw->transfer_length = 8u;
    cbw->flags = 0x80u;
    cbw->cdb_length = 10u;
    cbw->cdb[0] = 0x25u;
}

static void build_rw10(NTZ_USB_MASS_CBW *cbw, u32 tag, u32 lba, u16 blocks, u32 byte_count, int write) {
    if (cbw == (NTZ_USB_MASS_CBW *)0) return;
    zero_cbw(cbw);
    cbw->signature = CBW_SIGNATURE;
    cbw->tag = tag;
    cbw->transfer_length = byte_count;
    cbw->flags = write ? 0x00u : 0x80u;
    cbw->cdb_length = 10u;
    cbw->cdb[0] = write ? 0x2Au : 0x28u;
    store_be32(&cbw->cdb[2], lba);
    store_be16(&cbw->cdb[7], blocks);
}

void UsbMassBuildRead10(NTZ_USB_MASS_CBW *cbw, u32 tag, u32 lba, u16 blocks, u32 byte_count) {
    build_rw10(cbw, tag, lba, blocks, byte_count, 0);
}

void UsbMassBuildWrite10(NTZ_USB_MASS_CBW *cbw, u32 tag, u32 lba, u16 blocks, u32 byte_count) {
    build_rw10(cbw, tag, lba, blocks, byte_count, 1);
}

int UsbMassValidateCsw(const NTZ_USB_MASS_CSW *csw, u32 expected_tag) {
    if (csw == (const NTZ_USB_MASS_CSW *)0) return 0;
    return csw->signature == CSW_SIGNATURE && csw->tag == expected_tag && csw->status == 0u;
}
