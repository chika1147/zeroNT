#!/usr/bin/env python3
"""Non-destructive QEMU/OVMF serial smoke test for the public NTZero Alpha tree.

This is a *boot-entry* test, not a claim that storage, GUI, or installation work.
Only Python's standard library is required.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = (
    "KiSystemStartup: x86_64 native kernel entered",
    "BOOT: UEFI ExitBootServices complete",
    "NTZero Alpha 0.1: READY",
)
FATAL = (
    "BOOT: invalid boot information",
    "IDT: initialization failed",
    "PMM: initialization failed",
    "EXCEPTION vector=",
)


def classify_serial(serial):
    """Return (passed, explanation) from an actual serial-log string."""
    for marker in FATAL:
        if marker in serial:
            return False, "Fatal kernel marker: " + marker
    positions = [serial.find(marker) for marker in EXPECTED]
    missing = [marker for marker, pos in zip(EXPECTED, positions) if pos < 0]
    if missing:
        return False, "Missing markers: " + ", ".join(missing)
    if positions != sorted(positions):
        return False, "Boot markers appear out of order"
    return True, "Kernel entry, ExitBootServices handoff, and READY found in order"


def find_qemu(explicit):
    candidates = [explicit] if explicit else [
        shutil.which("qemu-system-x86_64"),
        shutil.which("qemu-system-x86_64.exe"),
        r"C:\Program Files\qemu\qemu-system-x86_64.exe",
    ]
    for candidate in candidates:
        if candidate and (Path(candidate).is_file() or shutil.which(candidate)):
            return str(candidate)
    raise FileNotFoundError("QEMU executable not found. Install QEMU or use --qemu.")


def find_ovmf(explicit):
    candidates = [explicit] if explicit else [
        os.environ.get("NTZERO_OVMF_CODE"),
        r"C:\Program Files\qemu\share\edk2-x86_64-code.fd",
        r"C:\Program Files\qemu\share\edk2-x86_64-secure-code.fd",
        "/usr/share/OVMF/OVMF_CODE.fd",
        "/usr/share/OVMF/OVMF_CODE_4M.fd",
        "/usr/share/edk2/ovmf/OVMF_CODE.fd",
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    raise FileNotFoundError("OVMF firmware not found. Set NTZERO_OVMF_CODE or use --ovmf.")


def run_qemu(args):
    esp = Path(args.esp).resolve()
    boot_efi = esp / "EFI" / "BOOT" / "BOOTX64.EFI"
    if not boot_efi.is_file():
        raise FileNotFoundError(
            str(boot_efi) + " missing; run scripts/build.ps1 first"
        )
    qemu = find_qemu(args.qemu)
    ovmf = find_ovmf(args.ovmf)
    log = Path(args.log).resolve()
    log.parent.mkdir(parents=True, exist_ok=True)
    # Never accept old serial output as proof of a new successful boot.
    log.write_bytes(b"")

    image = ROOT / "build" / "nvme-test.img"
    image.parent.mkdir(parents=True, exist_ok=True)
    if not image.exists():
        with image.open("wb") as target:
            target.truncate(64 * 1024 * 1024)

    cmd = [
        qemu, "-machine", "q35", "-m", "2048", "-smp", "4",
        "-bios", ovmf,
        "-drive", "format=raw,file=fat:rw:" + str(esp),
        "-drive", "file=" + str(image) + ",if=none,format=raw,id=nvmedisk",
        "-device", "nvme,drive=nvmedisk,serial=NTZERO0001",
        "-device", "qemu-xhci,id=xhci",
        "-net", "none",
        "-netdev", "user,id=e1000net",
        "-device", "e1000,netdev=e1000net",
        "-netdev", "user,id=virtionet",
        "-device", "virtio-net-pci,netdev=virtionet",
        "-device", "virtio-rng-pci",
        "-display", "none", "-monitor", "none",
        "-serial", "file:" + str(log),
        "-no-reboot", "-no-shutdown",
    ]
    print("QEMU:", qemu, flush=True)
    print("OVMF:", ovmf, flush=True)
    print("ESP:", esp, flush=True)
    print("Serial log:", log, flush=True)
    proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    started = time.monotonic()
    result = None
    try:
        while time.monotonic() - started < args.timeout:
            serial = log.read_text(encoding="utf-8", errors="replace")
            passed, why = classify_serial(serial)
            if passed:
                result = (True, why)
                break
            if any(fatal in serial for fatal in FATAL):
                result = (False, why)
                break
            if proc.poll() is not None:
                result = (False, "QEMU exited before READY, exit code " + str(proc.returncode))
                break
            time.sleep(0.25)
        if result is None:
            serial = log.read_text(encoding="utf-8", errors="replace")
            _, why = classify_serial(serial)
            result = (False, "Timed out after " + str(args.timeout) + " s; " + why)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
        if not result or not result[0]:
            details = proc.stderr.read().decode("utf-8", errors="replace").strip()
            if details:
                print("QEMU stderr:", details[-2000:], file=sys.stderr)
        proc.stderr.close()
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-log", help="Check an existing serial file (no QEMU)")
    parser.add_argument("--qemu", help="Path to QEMU executable")
    parser.add_argument("--ovmf", help="Path to OVMF code firmware")
    parser.add_argument("--esp", default=str(ROOT / "build" / "esp"))
    parser.add_argument("--log", default=str(ROOT / "build" / "qemu-boot-serial.log"))
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args(argv)
    if args.timeout < 1 or args.timeout > 900:
        parser.error("--timeout must be between 1 and 900 seconds")
    try:
        if args.check_log:
            serial = Path(args.check_log).read_text(encoding="utf-8", errors="replace")
            passed, explanation = classify_serial(serial)
        else:
            passed, explanation = run_qemu(args)
    except (OSError, ValueError) as error:
        print("ERROR:", error, file=sys.stderr)
        return 2
    print(("PASS: " if passed else "FAIL: ") + explanation, flush=True)
    if passed:
        print("Scope: kernel startup smoke only; device I/O, GUI, and installation unverified.")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
