# zeroNT v32.16 — QEMU LOWMEM FIX

Launcher-only compatibility fix based on v32.15.

- Keeps zeroNT v32.15 kernel, UI, boot pacing, drivers and installed-disk behavior unchanged.
- Forces QEMU TCG translation-block cache to 64 MiB with `-accel "tcg,tb-size=64"`.
- Avoids QEMU's 1 GiB default system-emulation JIT buffer allocation on hosts with a small Windows paging-file/commit limit.
- Guest RAM remains 512 MiB.
- No zeroNT runtime feature was removed.

If an existing `run/zeront-sata.img` contains your installation, copy it into this version's `run` folder and start with `./run.ps1` (without `-ResetDisk`).
