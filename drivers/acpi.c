#include "ntzero/drivers.h"

typedef struct __attribute__((packed)) ACPI_RSDP {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
} ACPI_RSDP;

typedef struct __attribute__((packed)) ACPI_SDT_HEADER {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} ACPI_SDT_HEADER;

static ACPI_SDT_HEADER *g_xsdt;

static int bytes_equal(const char *a, const char *b, usize count) {
    usize i;
    for (i = 0u; i < count; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int acpi_checksum_ok(const void *ptr, usize length) {
    const u8 *bytes = (const u8 *)ptr;
    u8 sum = 0u;
    usize i;
    if (ptr == (const void *)0 || length == 0u) return 0;
    for (i = 0u; i < length; ++i) sum = (u8)(sum + bytes[i]);
    return sum == 0u;
}

static int sdt_valid(const ACPI_SDT_HEADER *header) {
    if (header == (const ACPI_SDT_HEADER *)0) return 0;
    if (header->length < (u32)sizeof(ACPI_SDT_HEADER)) return 0;
    return acpi_checksum_ok(header, header->length);
}

NTSTATUS AcpiInitialize(void *rsdp_ptr) {
    ACPI_RSDP *rsdp = (ACPI_RSDP *)rsdp_ptr;
    g_xsdt = (ACPI_SDT_HEADER *)0;
    if (rsdp == (ACPI_RSDP *)0) return STATUS_NOT_FOUND;
    if (!bytes_equal(rsdp->signature, "RSD PTR ", 8u)) return STATUS_INVALID_PARAMETER;
    if (!acpi_checksum_ok(rsdp, 20u)) return STATUS_INVALID_PARAMETER;
    if (rsdp->revision < 2u || rsdp->length < (u32)sizeof(ACPI_RSDP)) return STATUS_NOT_FOUND;
    if (!acpi_checksum_ok(rsdp, rsdp->length)) return STATUS_INVALID_PARAMETER;
    if (rsdp->xsdt_address == 0u) return STATUS_NOT_FOUND;
    g_xsdt = (ACPI_SDT_HEADER *)(usize)rsdp->xsdt_address;
    if (!bytes_equal(g_xsdt->signature, "XSDT", 4u) || !sdt_valid(g_xsdt)) {
        g_xsdt = (ACPI_SDT_HEADER *)0;
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

void *AcpiFindTable(const char signature[4]) {
    const u64 *entries;
    u32 count;
    u32 i;
    if (g_xsdt == (ACPI_SDT_HEADER *)0 || signature == (const char *)0) return (void *)0;
    count = (g_xsdt->length - (u32)sizeof(ACPI_SDT_HEADER)) / 8u;
    entries = (const u64 *)((const u8 *)g_xsdt + sizeof(ACPI_SDT_HEADER));
    for (i = 0u; i < count; ++i) {
        ACPI_SDT_HEADER *header = (ACPI_SDT_HEADER *)(usize)entries[i];
        if (header != (ACPI_SDT_HEADER *)0 && bytes_equal(header->signature, signature, 4u) && sdt_valid(header)) {
            return header;
        }
    }
    return (void *)0;
}
