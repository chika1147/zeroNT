# zeroNT v32 Build/Test Results

Performed in the build container:

- Clean BOOTX64.EFI compile/link: PASS
- Clean kernel compile/link: PASS
- Warning scan: PASS (0 warnings)
- Native FAT32 host regression: PASS
  - mount
  - create
  - write
  - append
  - read-back
  - mkdir
  - nested file
  - rename
  - delete
  - rmdir
- SafeFS journal recovery simulation: PASS
  - original file moved to backup
  - verified temp file present
  - transaction journal left at stage 2
  - boot recovery restored original data
- User-system host regression: PASS
  - first-admin setup
  - wrong-password rejection
  - case-insensitive login
  - display-name change
  - password change
  - permission check
  - C:\\Users\\<name> logical path -> FAT32 backing-profile resolution
- rootfs metadata check: PASS
  - `/SYSTEM/VERSION.TXT`
  - `/SYSTEM/BETA.CFG`
  - CMD manifest
  - Services manifest
  - Event Viewer manifest

Runtime/QEMU tests are not marked PASS here because QEMU/OVMF is not installed in this build container. Use `run.ps1` and the beta test plan on the Windows development PC.
