CLANGXX ?= clang++
CLANG   ?= clang
LLDLINK ?= lld-link
LDLLD   ?= ld.lld
OBJCOPY ?= llvm-objcopy

KERNEL_CXXFLAGS = --target=x86_64-unknown-none-elf -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -mno-red-zone -fno-stack-protector -fpie -fno-plt -nostdlib -mgeneral-regs-only -Wall -Wextra -Wno-unused-parameter -Ishared -Ikernel
KERNEL_OBJS = build/kernel.o build/classic_ui.o build/memory.o build/paging.o build/heap.o build/block_device.o build/pci.o build/ahci.o build/fat32.o build/process.o build/scheduler.o build/usermode.o build/syscall.o build/executable.o build/gui.o build/driver_manager.o build/nvme.o build/usb.o build/acpi.o build/platform_hardware.o build/display.o build/audio.o build/network.o build/user_manager.o build/system_apps.o build/quality.o build/security.o build/safe_fs.o build/event_log.o build/beta.o build/installer.o build/interrupts.o build/runtime.o build/arch.o


USER_APP_NAMES = EXPLORER NOTEPAD CMD CONTROL TASKMGR DEVMGMT RECYCLE SERVICES EVENTVWR MYCOMP NETWORK MYDOCS PRINTERS
USER_APP_KINDS = 1 2 3 4 5 6 7 8 9 10 11 12 13
USER_APP_CXXFLAGS = --target=x86_64-unknown-none-elf -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -fno-stack-protector -fpie -fno-plt -nostdlib -mgeneral-regs-only -Os -ffunction-sections -fdata-sections -Iapps

all: build/BOOTX64.EFI build/kernel.elf userapps rootfs-apps stage disk/boot-media.img

build:
	mkdir -p build

build/bootloader.obj: bootloader/main.cpp bootloader/efi.hpp shared/bootinfo.hpp | build
	$(CLANGXX) -target x86_64-pc-windows-msvc -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fshort-wchar -mno-red-zone -fno-stack-protector -Wall -Wextra -Wno-unused-parameter -Ibootloader -Ishared -c $< -o $@

build/BOOTX64.EFI: build/bootloader.obj
	$(LLDLINK) /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:x64 /out:$@ $<

build/kernel.o: kernel/kernel.cpp shared/bootinfo.hpp kernel/include/arch.hpp kernel/include/memory.hpp kernel/include/paging.hpp kernel/include/heap.hpp kernel/include/interrupts.hpp kernel/include/scheduler.hpp kernel/include/process.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/classic_ui.o: kernel/classic_ui.cpp kernel/include/classic_ui.hpp kernel/include/memory.hpp shared/bootinfo.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/memory.o: kernel/memory.cpp shared/bootinfo.hpp kernel/include/memory.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/paging.o: kernel/paging.cpp shared/bootinfo.hpp kernel/include/paging.hpp kernel/include/memory.hpp kernel/include/arch.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/heap.o: kernel/heap.cpp kernel/include/heap.hpp kernel/include/paging.hpp kernel/include/memory.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/block_device.o: kernel/block_device.cpp kernel/include/block_device.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/pci.o: kernel/pci.cpp kernel/include/pci.hpp kernel/include/arch.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/ahci.o: kernel/ahci.cpp kernel/include/ahci.hpp kernel/include/pci.hpp kernel/include/block_device.hpp kernel/include/memory.hpp kernel/include/paging.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/fat32.o: kernel/fat32.cpp kernel/include/fat32.hpp kernel/include/block_device.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/process.o: kernel/process.cpp kernel/include/process.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/usermode.o: kernel/usermode.cpp kernel/include/usermode.hpp kernel/include/syscall.hpp kernel/include/paging.hpp kernel/include/memory.hpp kernel/include/arch.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/syscall.o: kernel/syscall.cpp kernel/include/syscall.hpp kernel/include/audio.hpp kernel/include/network.hpp kernel/include/security.hpp kernel/include/system_apps.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/executable.o: kernel/executable.cpp kernel/include/executable.hpp kernel/include/usermode.hpp kernel/include/fat32.hpp kernel/include/heap.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/gui.o: kernel/gui.cpp kernel/include/gui.hpp shared/bootinfo.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/scheduler.o: kernel/scheduler.cpp kernel/include/scheduler.hpp kernel/include/process.hpp kernel/include/heap.hpp kernel/include/arch.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/driver_manager.o: kernel/driver_manager.cpp kernel/include/driver_manager.hpp kernel/include/pci.hpp kernel/include/nvme.hpp kernel/include/usb.hpp kernel/include/acpi.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/nvme.o: kernel/nvme.cpp kernel/include/nvme.hpp kernel/include/pci.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/usb.o: kernel/usb.cpp kernel/include/usb.hpp kernel/include/pci.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/acpi.o: kernel/acpi.cpp kernel/include/acpi.hpp kernel/include/arch.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/platform_hardware.o: kernel/platform_hardware.cpp kernel/include/platform_hardware.hpp kernel/include/acpi.hpp kernel/include/pci.hpp kernel/include/paging.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/display.o: kernel/display.cpp kernel/include/display.hpp kernel/include/pci.hpp kernel/include/arch.hpp kernel/include/paging.hpp kernel/include/classic_ui.hpp kernel/include/gui.hpp kernel/include/fat32.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/audio.o: kernel/audio.cpp kernel/include/audio.hpp kernel/include/pci.hpp kernel/include/arch.hpp kernel/include/paging.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/network.o: kernel/network.cpp kernel/include/network.hpp kernel/include/pci.hpp kernel/include/memory.hpp kernel/include/paging.hpp kernel/include/arch.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/user_manager.o: kernel/user_manager.cpp kernel/include/user_manager.hpp kernel/include/fat32.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/system_apps.o: kernel/system_apps.cpp kernel/include/system_apps.hpp kernel/include/user_manager.hpp kernel/include/fat32.hpp kernel/include/process.hpp kernel/include/scheduler.hpp kernel/include/driver_manager.hpp kernel/include/network.hpp kernel/include/nvme.hpp kernel/include/usb.hpp kernel/include/usermode.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/quality.o: kernel/quality.cpp kernel/include/quality.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/security.o: kernel/security.cpp kernel/include/security.hpp kernel/include/paging.hpp kernel/include/quality.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/safe_fs.o: kernel/safe_fs.cpp kernel/include/safe_fs.hpp kernel/include/fat32.hpp kernel/include/heap.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/event_log.o: kernel/event_log.cpp kernel/include/event_log.hpp kernel/include/safe_fs.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/beta.o: kernel/beta.cpp kernel/include/beta.hpp kernel/include/event_log.hpp kernel/include/safe_fs.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@


build/installer.o: kernel/installer.cpp kernel/include/installer.hpp kernel/include/block_device.hpp kernel/include/fat32.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/interrupts.o: kernel/interrupts.cpp kernel/include/arch.hpp kernel/include/interrupts.hpp kernel/include/scheduler.hpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/runtime.o: kernel/runtime.cpp | build
	$(CLANGXX) $(KERNEL_CXXFLAGS) -c $< -o $@

build/arch.o: kernel/arch.S | build
	$(CLANG) --target=x86_64-unknown-none-elf -ffreestanding -mno-red-zone -c $< -o $@

build/kernel.elf: $(KERNEL_OBJS) kernel/linker.ld
	$(LDLLD) -nostdlib -pie --no-dynamic-linker -Bsymbolic -z max-page-size=0x1000 -T kernel/linker.ld $(KERNEL_OBJS) -o $@

userapps: | build
	mkdir -p build/userapps
	@set -e; \
	i=1; \
	for n in $(USER_APP_NAMES); do \
	  echo "USER-EXE $$n (kind=$$i)"; \
	  $(CLANGXX) $(USER_APP_CXXFLAGS) -DAPP_KIND=$$i -c apps/system_app.cpp -o build/userapps/$$n.o; \
	  $(LDLLD) -nostdlib -static --gc-sections -T apps/linker.ld --oformat=binary build/userapps/$$n.o -o build/userapps/$$n.bin; \
	  i=$$((i+1)); \
	done

build/inject_apps: tools/inject_apps.cpp kernel/block_device.cpp kernel/fat32.cpp | build
	$(CLANGXX) -std=c++17 -O2 -Ikernel $^ -o $@

rootfs-apps: build/inject_apps userapps build/BOOTX64.EFI build/kernel.elf
	build/inject_apps disk/System/rootfs.img build/userapps build/BOOTX64.EFI build/kernel.elf


build/fat32_tree_test: tools/fat32_tree_test.cpp kernel/block_device.cpp kernel/fat32.cpp | build
	$(CLANGXX) -std=c++17 -O2 -Ikernel $^ -o $@

build/fat32_lfn_test: tools/fat32_lfn_test.cpp kernel/block_device.cpp kernel/fat32.cpp | build
	$(CLANGXX) -std=c++17 -O2 -Ikernel $^ -o $@

test-fat32-lfn: build/fat32_lfn_test
	build/fat32_lfn_test disk/System/rootfs.img

build/rootfs_verify: tools/rootfs_verify.cpp kernel/block_device.cpp kernel/fat32.cpp | build
	$(CLANGXX) -std=c++17 -O2 -Ikernel $^ -o $@

verify-rootfs: rootfs-apps build/rootfs_verify
	build/rootfs_verify disk/System/rootfs.img

build/mkbootmedia: tools/mkbootmedia.cpp | build
	$(CLANGXX) -std=c++17 -O2 $< -o $@

stage: build/BOOTX64.EFI build/kernel.elf rootfs-apps
	mkdir -p disk/EFI/BOOT disk/System
	cp build/BOOTX64.EFI disk/EFI/BOOT/BOOTX64.EFI
	cp build/kernel.elf disk/System/KERNEL.ELF
	test -f disk/System/rootfs.img

disk/boot-media.img: stage build/mkbootmedia
	build/mkbootmedia $@ disk/EFI/BOOT/BOOTX64.EFI build/kernel.elf disk/System/rootfs.img

clean:
	rm -rf build run disk/EFI/BOOT/BOOTX64.EFI disk/System/KERNEL.ELF disk/boot-media.img

test-fat32-tree: build/fat32_tree_test
	build/fat32_tree_test disk/System/rootfs.img

.PHONY: all stage clean userapps rootfs-apps test-fat32-tree test-fat32-lfn verify-rootfs

build/installer_test: tools/installer_test.cpp kernel/block_device.cpp kernel/fat32.cpp kernel/installer.cpp | build
	$(CLANGXX) -std=c++17 -O2 -Ikernel $^ -o $@

test-installer: rootfs-apps build/installer_test
	build/installer_test disk/System/rootfs.img

.PHONY: test-installer
