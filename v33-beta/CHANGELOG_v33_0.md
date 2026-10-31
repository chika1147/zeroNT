# v33.0 changelog

- consolidated roadmap phases 1-6 into one development branch
- ELF64 static-PIE kernel loader with dynamic physical base and RELATIVE relocations
- removed multi-base raw-kernel dependency from generated boot media
- streaming FAT32 copy/move API and free-space API
- Explorer file clipboard no longer buffers the complete file
- installer target read-back verification
- per-ZEX PID lifecycle and fresh Ring3 code/stack pages
- syscall GUI ownership bound to active ZEX PID
- CMD `RUN <app>` support
- build-time injection of HELLO.ZEX and WINDOW.ZEX
- original classic UI/mouse/quiet-rendering/100 ms boot pacing retained

## Phase 1-10 Beta integration update
The current package continues v33.0 Beta with the Phase 1-10 upgrade pass.
See `CHANGELOG_v33_0_PHASE1_10.md` for the NVMe, xHCI, recursive FAT32,
private-CR3 Ring-3, TCP/HTTP, audio, GUI, Explorer/CMD/Settings changes.
