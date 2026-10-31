# zeroNT v33.0 Beta — Network + Audio Upgrade

This package continues the v33.0 Beta public-release work after the Hardware + Memory upgrade.

## Phase 3 — Network

- Expanded Intel e1000 PCI matching for QEMU/VMware-oriented virtual NIC IDs.
- RX/TX descriptor rings enlarged and TX descriptor ownership is checked before reuse.
- Link state is re-checked while the OS is running.
- Link-down closes active sockets and clears stale ARP state.
- Link-up restarts DHCP automatically unless static addressing is configured.
- Removed the old hard-coded QEMU 10.0.2.15 fallback from normal DHCP failure handling.
- Added explicit DHCP failure state and DHCP restart API.
- Added static IPv4 configuration API.
- Added ARP neighbor cache and next-hop routing for local-subnet vs gateway traffic.
- DNS now routes through the normal next-hop logic instead of assuming the DNS server is directly reachable.
- Added small DNS A-record cache with TTL-bounded expiry and cache flush support.
- Added four concurrent polling TCP client slots.
- TCP SYN/data/FIN retransmission paths and timeout counters added.
- Added four UDP sockets with open/send/receive/close APIs.
- Existing DHCP/DNS/ICMP/HTTP paths remain available.
- Ring-3 syscalls now expose DNS, TCP and UDP socket operations plus DHCP restart.
- CMD additions: DHCP, DNSFLUSH, NETSTAT, expanded IPCONFIG.
- Control Panel adds DHCP renew and richer network status.

## Phase 4 — Audio

- AC97 playback changed from one-shot tone DMA to a queued PCM engine.
- 48 kHz signed 16-bit stereo software queue.
- PCM submission accepts 8 kHz–192 kHz input and resamples to the 48 kHz hardware stream.
- Chunked DMA playback continues from audio_poll().
- Volume and mute support.
- Stop/flush support.
- Startup / Notify / Error / Click system sounds.
- New Ring-3 syscalls for PCM submission, stop, status and mute.
- CMD additions: MUTE, SOUND, expanded AUDIO status.
- HDA devices are detected and reported safely, but a full Intel HDA codec/stream driver is not included in this package.

## Validation scope

The source tree clean-builds with Clang/LLD in the artifact environment, rootfs verification passes, FAT32 recursive tests pass, and the VMware/UEFI ISO El Torito boot image structure validates. Runtime audio/network behavior still requires QEMU/VMware or physical-hardware testing.
