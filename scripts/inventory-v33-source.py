#!/usr/bin/env python3
"""Read-only zeroNT source inventory. Never extracts or executes ZIP contents."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import zipfile

SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".asm", ".s", ".ps1", ".py", ".bat", ".ld", ".lds"}
TARGET_WORDS = ("boot", "kernel", "fat32", "fs", "installer", "install", "gui", "desktop", "driver", "uefi", "elf")


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_member(name):
    normalized = name.replace("\\", "/")
    parts = PurePosixPath(normalized).parts
    return bool(parts) and not normalized.startswith("/") and not any(part in ("..", ".") for part in parts) and not (len(normalized) >= 2 and normalized[1] == ":")


def inventory(path):
    path = Path(path).resolve()
    if not path.exists():
        raise FileNotFoundError(path)
    if path.is_file():
        if not zipfile.is_zipfile(path):
            raise ValueError("Expected a directory or valid .zip archive")
        with zipfile.ZipFile(path) as archive:
            names = sorted(item.filename.replace("\\", "/") for item in archive.infolist() if not item.is_dir() and safe_member(item.filename))
        source_digest = sha256(path)
        kind = "zip"
    elif path.is_dir():
        names = sorted(str(p.relative_to(path)).replace("\\", "/") for p in path.rglob("*") if p.is_file() and not p.is_symlink())
        source_digest = None
        kind = "directory"
    else:
        raise ValueError("Expected an ordinary directory or ZIP")
    src = [name for name in names if Path(name).suffix.lower() in SOURCE_SUFFIXES]
    candidate = [name for name in src if any(word in name.lower() for word in TARGET_WORDS)]
    return {
        "source": str(path),
        "kind": kind,
        "archive_sha256": source_digest,
        "files_total": len(names),
        "source_files": len(src),
        "candidate_paths": candidate,
        "note": "Read-only inventory only; no compile, boot, or compatibility verification performed.",
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", help="zeroNT v33.0 Beta source directory or ZIP")
    parser.add_argument("--output", help="Optional JSON output path; keep private until source is reviewed")
    args = parser.parse_args(argv)
    try:
        data = inventory(args.source)
        report = json.dumps(data, ensure_ascii=False, indent=2)
        if args.output:
            output = Path(args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(report + "\n", encoding="utf-8")
        print(report)
        return 0
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print("ERROR:", error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
