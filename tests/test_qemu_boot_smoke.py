"""Host tests for the read-only boot log and v33 source inventory utilities."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load_script(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


smoke = load_script("boot_smoke", ROOT / "scripts" / "test-qemu-boot.py")
source = load_script("v33_inventory", ROOT / "scripts" / "inventory-v33-source.py")
GOOD = (
    "NTZero Alpha 0.1\n"
    "KiSystemStartup: x86_64 native kernel entered\n"
    "BOOT: UEFI ExitBootServices complete\n"
    "IDT: exceptions + IRQ/MSI vector table installed (interrupts masked)\n"
    "ALPC: kernel transport initialized\n"
    "NTZero Alpha 0.1: READY\n"
)


class BootSerialTests(unittest.TestCase):
    def test_good_log_passes(self):
        passed, _ = smoke.classify_serial(GOOD)
        self.assertTrue(passed)

    def test_missing_ready_fails(self):
        passed, _ = smoke.classify_serial(GOOD.replace("NTZero Alpha 0.1: READY", ""))
        self.assertFalse(passed)

    def test_invalid_boot_info_fails_even_if_ready_present(self):
        passed, _ = smoke.classify_serial(GOOD + "BOOT: invalid boot information\n")
        self.assertFalse(passed)

    def test_out_of_order_fails(self):
        serial = ("NTZero Alpha 0.1: READY\n" +
                  "KiSystemStartup: x86_64 native kernel entered\n" +
                  "BOOT: UEFI ExitBootServices complete\n")
        passed, _ = smoke.classify_serial(serial)
        self.assertFalse(passed)


class V33InventoryTests(unittest.TestCase):
    def test_source_inventory_reads_zip_without_extracting(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "v33-source.zip"
            with zipfile.ZipFile(archive, "w") as data:
                data.writestr("boot/uefi/boot.c", "/* sample */\n")
                data.writestr("kernel/entry.c", "/* sample */\n")
                data.writestr("build/kernel.elf", "ELF marker\n")
            report = source.inventory(archive)
            self.assertEqual(report["kind"], "zip")
            self.assertEqual(report["files_total"], 3)
            self.assertEqual(report["source_files"], 2)
            self.assertEqual(len(report["archive_sha256"]), 64)
            self.assertIn("boot/uefi/boot.c", report["candidate_paths"])

    def test_nested_stage1234_source_archive(self):
        import io
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "v33-ALL.zip"
            payload = io.BytesIO()
            with zipfile.ZipFile(payload, "w") as inner:
                inner.writestr("zeront_v33_stage1234_fixed/kernel/kernel.cpp", "int test;")
                inner.writestr("zeront_v33_stage1234_fixed/bootloader/main.cpp", "int test;")
            inner_bytes = payload.getvalue()
            with zipfile.ZipFile(archive, "w") as outer:
                outer.writestr("package/zeroNT_v33_0_BETA_STAGE1234_FIX_FULL_SOURCE.zip", inner_bytes)
                outer.writestr("package/boot.img", "pretend disk")
            report = source.inventory(archive)
            self.assertEqual(report["kind"], "nested_source_zip")
            self.assertEqual(report["source_files"], 2)
            self.assertEqual(report["nested_source_sha256"], hashlib.sha256(inner_bytes).hexdigest())
            self.assertEqual(report["files_total"], 2)

    def test_rejects_zip_traversal_paths(self):
        self.assertFalse(source.safe_member("../secrets.txt"))
        self.assertFalse(source.safe_member("/etc/passwd"))
        self.assertFalse(source.safe_member("C:/bad"))
        self.assertTrue(source.safe_member("kernel/main.c"))


if __name__ == "__main__":
    unittest.main()
