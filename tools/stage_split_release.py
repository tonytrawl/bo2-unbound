"""Stage one update-only Black Ops 2 Unbound HQ core release for GitHub.

The input is one Wii U title dump containing 0005000e/<title-id> (UPDATE) and
optionally 0005000c/<title-id> (legacy AOC content). Both content trees are
packaged below update/content. The source language folder is renamed to
@language so HQ can route localized files on-console.

The historical filename is kept so existing shortcuts continue to work.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile
import zlib


GITHUB_ASSET_LIMIT = 2 * 1024 * 1024 * 1024
MAX_FILE_BYTES = 2 * 1024 * 1024 * 1024
MAX_UNCOMPRESSED_BYTES = 5 * 1024 * 1024 * 1024
MAX_FILES = 4096
EXCLUDED_SUFFIXES = (".bak",)
COMPRESSION_SAMPLE_BYTES = 256 * 1024
STORE_RATIO_THRESHOLD = 0.97


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def compression_for_file(path: Path) -> int:
    size = path.stat().st_size
    if size < 64 * 1024:
        return zipfile.ZIP_DEFLATED
    sample_size = min(COMPRESSION_SAMPLE_BYTES, size)
    offsets = {0, max(0, size // 2 - sample_size // 2), max(0, size - sample_size)}
    raw_bytes = 0
    compressed_bytes = 0
    with path.open("rb") as stream:
        for offset in sorted(offsets):
            stream.seek(offset)
            sample = stream.read(sample_size)
            raw_bytes += len(sample)
            compressed_bytes += len(zlib.compress(sample, level=1))
    if raw_bytes and compressed_bytes >= int(raw_bytes * STORE_RATIO_THRESHOLD):
        return zipfile.ZIP_STORED
    return zipfile.ZIP_DEFLATED


def collect(root: Path, archive_root: str, source_language: str) -> tuple[list[tuple[Path, str]], list[str]]:
    if not root.is_dir():
        raise RuntimeError(f"Missing required source directory: {root}")
    files: list[tuple[Path, str]] = []
    excluded: list[str] = []
    archive_names: set[str] = set()
    for source in sorted(root.rglob("*"), key=lambda value: value.as_posix().casefold()):
        if not source.is_file():
            continue
        relative_parts = list(source.relative_to(root).parts)
        relative_parts = ["@language" if part.casefold() == source_language.casefold() else part
                          for part in relative_parts]
        relative = "/".join(relative_parts)
        archive_name = f"{archive_root}/{relative}"
        if source.name.lower().endswith(EXCLUDED_SUFFIXES) or " - copy." in source.name.lower():
            excluded.append(archive_name)
            continue
        if archive_name == "update/content/update.txt":
            excluded.append(archive_name)
            continue
        archive_key = archive_name.casefold()
        if archive_key in archive_names:
            raise RuntimeError(f"More than one source file maps to {archive_name}")
        archive_names.add(archive_key)
        files.append((source, archive_name))
    if not files:
        raise RuntimeError(f"No release files found below {root}")
    return files, excluded


def write_archive(destination: Path, files: list[tuple[Path, str]]) -> None:
    if len(files) > MAX_FILES:
        raise RuntimeError(f"Merged payload has {len(files)} files; HQ accepts at most {MAX_FILES}")
    uncompressed = 0
    for source, archive_name in files:
        size = source.stat().st_size
        if size > MAX_FILE_BYTES:
            raise RuntimeError(
                f"Payload file {archive_name} is {size} bytes; the per-file limit is "
                f"2 GiB ({MAX_FILE_BYTES} bytes)"
            )
        uncompressed += size
    if uncompressed > MAX_UNCOMPRESSED_BYTES:
        raise RuntimeError(
            f"Merged payload is {uncompressed} bytes; HQ accepts at most "
            f"5 GiB ({MAX_UNCOMPRESSED_BYTES} bytes) total uncompressed"
        )
    temporary = destination.with_suffix(destination.suffix + ".part")
    temporary.unlink(missing_ok=True)
    print(f"Writing {destination.name} ({len(files)} files)", flush=True)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9, allowZip64=True) as archive:
            for index, (source, archive_name) in enumerate(files, 1):
                info = zipfile.ZipInfo(archive_name, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = compression_for_file(source)
                info.external_attr = 0o100644 << 16
                with source.open("rb") as input_stream, archive.open(info, "w", force_zip64=True) as output_stream:
                    shutil.copyfileobj(input_stream, output_stream, length=4 * 1024 * 1024)
                print(f"  [{index}/{len(files)}] {archive_name}", flush=True)
        if temporary.stat().st_size >= GITHUB_ASSET_LIMIT:
            raise RuntimeError(f"{destination.name} must be below the 2 GiB compressed ZIP limit")
        temporary.replace(destination)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def merge_files(*groups: list[tuple[Path, str]]) -> list[tuple[Path, str]]:
    merged: list[tuple[Path, str]] = []
    names: set[str] = set()
    for group in groups:
        for source, archive_name in group:
            key = archive_name.casefold()
            if key in names:
                raise RuntimeError(f"More than one source file maps to {archive_name}")
            names.add(key)
            merged.append((source, archive_name))
    merged.sort(key=lambda item: item[1].casefold())
    return merged


def file_records(files: list[tuple[Path, str]]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for index, (source, archive_name) in enumerate(files, 1):
        print(f"Hashing [{index}/{len(files)}] {archive_name}", flush=True)
        records.append({"path": archive_name, "size": source.stat().st_size, "sha256": sha256_file(source)})
    return records


def write_text(path: Path, value: str) -> None:
    path.write_text(value, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--title-id", default="1010cf00")
    parser.add_argument("--package-key", default="UNIVERSAL")
    parser.add_argument("--source-language", default="english")
    parser.add_argument("--version", default="0.0.05")
    parser.add_argument("--tag", default="v0.0.05")
    parser.add_argument("--owner", default="tonytrawl")
    parser.add_argument("--repository", default="bo2-unbound")
    args = parser.parse_args()

    source = args.source.resolve()
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise RuntimeError(f"Output directory must be empty: {output}")
    output.mkdir(parents=True, exist_ok=True)

    update_code, excluded_code = collect(source / "0005000e" / args.title_id / "code", "update/code", args.source_language)
    update_content, excluded_content = collect(source / "0005000e" / args.title_id / "content", "update/content", args.source_language)
    aoc_root = source / "0005000c" / args.title_id / "content" / "0010"
    if aoc_root.is_dir():
        aoc_content, excluded_aoc = collect(aoc_root, "update/content", args.source_language)
    else:
        aoc_content, excluded_aoc = [], []
    update_files = merge_files(update_code, update_content, aoc_content)
    excluded = excluded_code + excluded_content + excluded_aoc

    package_slug = args.package_key.casefold()
    prefix = f"unbound-{package_slug}-{args.version}"
    update_zip = output / f"{prefix}-full.zip"
    write_archive(update_zip, update_files)

    update_hash = sha256_file(update_zip)
    update_records = file_records(update_files)
    base_url = f"https://github.com/{args.owner}/{args.repository}/releases/download/{args.tag}"
    created = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    manifest = {
        "schemaVersion": 4,
        "createdAt": created,
        "packageKey": args.package_key,
        "sourceTitleId": args.title_id,
        "languageToken": "@language",
        "supportedLanguageFolders": ["english", "french", "spanish", "italian", "german", "japanese"],
        "version": args.version,
        "tag": args.tag,
        "components": [
            {"name": "update", "assetName": update_zip.name, "downloadUrl": f"{base_url}/{update_zip.name}",
             "sha256": update_hash, "byteSize": update_zip.stat().st_size,
             "uncompressedBytes": sum(item["size"] for item in update_records), "files": update_records},
        ],
        "excludedSourcePaths": excluded,
    }
    manifest_name = f"{prefix}.manifest.json"
    write_text(output / manifest_name, json.dumps(manifest, indent=2) + "\n")
    releases_line = "|".join((args.package_key, args.version, f"{base_url}/{update_zip.name}", update_hash))
    write_text(output / "releases-update-only.txt",
               "# PACKAGE_KEY|VERSION|UPDATE_URL|UPDATE_SHA256\n" + releases_line + "\n")
    write_text(output / "checksums.sha256",
               f"{update_hash}  {update_zip.name}\n"
               f"{sha256_file(output / manifest_name)}  {manifest_name}\n")
    write_text(output / "RELEASE_NOTES.md",
               f"# Black Ops 2 Unbound HQ {args.tag}\n\n"
               "First public universal beta core release for supported Black Ops II Wii U title IDs.\n\n"
               "HQ detects the installed title ID and maps every `@language` archive path to the active game language. "
               "This release installs all supplied files into the registered UPDATE title. "
               "Legacy AOC content is relocated beneath `update/content`. The HQ app writes "
               "`update/content/update.txt` only after the package installs successfully.\n\n"
               "Excluded local backup files are listed in the package manifest.\n")
    print(f"COMPLETE update={update_zip.stat().st_size}", flush=True)
    print(f"UPDATE_SHA256={update_hash}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
