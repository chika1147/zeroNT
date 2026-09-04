# Historical NTZero v5.1 validation

This is a preserved validation record from the earlier v5.1 development artifact. It is separate from the public Alpha 0.1 source validation.

```text
OK: GPT CRC/FAT32 prebuilt/v5.1/ntzero-system-gpt.img
Stage 51: clang/lld build PASS with -Wall -Wextra -Werror
BOOTX64.EFI: PE32+ EFI x86-64
ntoskrnl.exe: PE32+ x86-64, NT Native, ImageBase 0x10000000
services.exe: PE32+ x86-64, NT Native, ImageBase 0x10000000000
ntzsvc.exe: PE32+ x86-64, NT Native, ImageBase 0x10000000000
Runtime QEMU handshake: not executed in artifact build environment (QEMU unavailable)
```
