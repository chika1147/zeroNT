# zeroNT v32.15 — 100 ms Boot Pacing

- Keeps the v32.14 original UI and mouse stability changes.
- Boot splash progress now advances in 5% steps.
- Each visible progress step is held for approximately 100 ms.
- Uses PIT channel 2 polling so pacing works before the normal scheduler timer starts.
- The 100 ms pacing is presentation-only: kernel, driver, storage, audio, network, user, security, filesystem and background processing are not removed.
- A timeout on the pacing timer prevents cosmetic timing from blocking boot on unusual hardware/emulators.
- First-install boots keep the Installer screen visible and do not repaint the boot splash over it.
