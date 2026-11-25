"""Package an already-built HQ hardware test without publishing or touching SD."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

PROJECT = Path(__file__).resolve().parent.parent
SD_PATH = "wiiu/apps/BO2-Unbound-HQ/BO2-Unbound-HQ.wuhb"


def sha(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", required=True, type=int)
    parser.add_argument("--notes", required=True, type=Path)
    args = parser.parse_args()
    revision = args.revision
    assert revision > 0
    version = f"0.0.{revision}-beta"
    configuration = (PROJECT / "include/config.hpp").read_text(encoding="utf-8")
    assert re.search(rf"AppReleaseRevision\s*=\s*{revision}\s*;", configuration)
    assert f'AppVersion = "{version}"' in configuration
    notes = args.notes.resolve().read_text(encoding="utf-8")
    source = PROJECT / f"BO2-Unbound-HQ-r{revision}.wuhb"
    size = source.stat().st_size
    assert 1024 * 1024 < size <= 64 * 1024 * 1024
    output = PROJECT / f"staging-r{revision}" / f"BO2-Unbound-HQ-v{version}-TEST"
    if output.exists():
        raise RuntimeError("Refusing to overwrite an existing handoff")
    output.mkdir(parents=True)

    def write(name: str, value: str) -> None:
        with (output / name).open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(value)

    wuhb = output / "BO2-Unbound-HQ.wuhb"
    shutil.copy2(source, wuhb)
    archive_path = output / f"BO2-Unbound-HQ-v{version}-TEST-sd.zip"
    with zipfile.ZipFile(archive_path, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(wuhb, SD_PATH)
    with zipfile.ZipFile(archive_path) as archive:
        assert archive.namelist() == [SD_PATH] and archive.testzip() is None
        assert hashlib.sha256(archive.read(SD_PATH)).hexdigest() == sha(wuhb)
    write("TEST_INSTRUCTIONS.txt", f"BLACK OPS 2 UNBOUND HQ {version} / r{revision}\n\n" + notes)
    sources = [PROJECT / "Makefile"]
    for directory, pattern in [("include", "*.hpp"), ("source", "*.hpp"), ("source", "*.cpp")]:
        sources.extend(sorted((PROJECT / directory).glob(pattern)))
    receipt = {
        "builtAt": datetime.now(timezone.utc).isoformat(), "version": version,
        "revision": revision, "status": "Hardware test",
        "wuhbBytes": size, "wuhbSha256": sha(wuhb),
        "sdZipBytes": archive_path.stat().st_size, "sdZipSha256": sha(archive_path),
        "sdZipOnlyPath": SD_PATH,
        "rpxSha256": sha(PROJECT / f"BO2-Unbound-HQ-r{revision}.rpx"),
        "elfSha256": sha(PROJECT / f"BO2-Unbound-HQ-r{revision}.elf"),
        "sourceSha256": {str(path.relative_to(PROJECT)).replace("\\", "/"): sha(path) for path in sources},
        "hardwareTestPerformed": False, "published": False, "committed": False,
        "sdCardChanged": False, "consoleFilesChanged": False,
    }
    write("BUILD_RECEIPT.json", json.dumps(receipt, indent=2) + "\n")
    files = [wuhb, archive_path, output / "TEST_INSTRUCTIONS.txt", output / "BUILD_RECEIPT.json"]
    write("checksums.sha256", "\n".join(f"{sha(path)}  {path.name}" for path in files) + "\n")
    print(json.dumps({key: receipt[key] for key in ["version", "revision", "wuhbBytes", "wuhbSha256", "sdZipBytes", "sdZipSha256"]}, indent=2))


if __name__ == "__main__":
    main()
