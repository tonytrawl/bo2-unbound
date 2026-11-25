"""Core package-building logic for Black Ops 2 Unbound.

The GUI imports this module, and the unit tests exercise it without creating a window.
Packages target only the registered Black Ops II update title. Legacy snapshots
that still contain aoc/content are migrated into update/content while scanning.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
from typing import Callable, Dict, Iterable, Optional
import zipfile
import zlib


REGIONS = ("UNIVERSAL",)
SNAPSHOT_ROOTS = (
    ("update/code", "update/code"),
    ("update/content", "update/content"),
    ("aoc/content/0010", "update/content"),
)
LANGUAGE_FOLDERS = ("english", "french", "spanish", "italian", "german", "japanese")
VERSION_RE = re.compile(r"^([0-9]+)\.([0-9]+)\.([0-9]+)(?:P([0-9]+(?:\.[0-9]+)*))?$")
OWNER_RE = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?$")
REPO_RE = re.compile(r"^[A-Za-z0-9._-]+$")
TAG_RE = re.compile(r"^[A-Za-z0-9._-]+$")
MAX_FILES = 4096
# The expanded payload may span several files, but each source file and the
# downloaded ZIP keep the existing 2 GiB storage/transfer safety boundary.
MAX_FILE_BYTES = 2 * 1024 * 1024 * 1024
MAX_ARCHIVE_BYTES = 2 * 1024 * 1024 * 1024
MAX_UNCOMPRESSED_BYTES = 5 * 1024 * 1024 * 1024
Progress = Callable[[str, int], None]
COMPRESSION_SAMPLE_BYTES = 256 * 1024
STORE_RATIO_THRESHOLD = 0.97


class PackageError(RuntimeError):
    pass


@dataclass(frozen=True)
class FileRecord:
    path: str
    source: str
    size: int
    sha256: str


@dataclass(frozen=True)
class BuildOptions:
    kind: str
    region: str
    version: str
    current_snapshot: str
    output_directory: str
    github_owner: str
    github_repository: str = "bo2-unbound"
    release_tag: str = ""
    previous_version: str = ""
    previous_snapshot: str = ""
    release_feed: str = ""


@dataclass(frozen=True)
class BuildResult:
    archive: str
    package_manifest: str
    catalog: str
    chain_releases: str
    release_record: str
    sha256: str
    included_files: int
    deleted_files: int
    compressed_bytes: int
    uncompressed_bytes: int
    release_feed_updated: bool
    release_feed_error: str


@dataclass(frozen=True)
class PackagePreview:
    archive_name: str
    manifest_name: str
    release_feed: str
    release_record: str
    included_paths: tuple[str, ...]
    deleted_paths: tuple[str, ...]
    uncompressed_bytes: int


def _report(callback: Optional[Progress], message: str, percent: int) -> None:
    if callback:
        callback(message, max(0, min(100, percent)))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _checked_payload_total(total: int, size: int, path: str) -> int:
    if total < 0 or size < 0:
        raise PackageError(f"Invalid payload size: {path}")
    if size > MAX_FILE_BYTES:
        raise PackageError(
            f"Snapshot file {path} is {size} bytes; the per-file limit is "
            f"2 GiB ({MAX_FILE_BYTES} bytes)"
        )
    if total > MAX_UNCOMPRESSED_BYTES or size > MAX_UNCOMPRESSED_BYTES - total:
        raise PackageError(
            "Snapshot exceeds the 5 GiB total uncompressed application limit "
            f"({MAX_UNCOMPRESSED_BYTES} bytes)"
        )
    return total + size


def compression_for_file(path: Path) -> int:
    """Store data that is clearly incompressible; deflate everything else.

    Three samples make this conservative for large mixed-content game archives. A wrong
    prediction can only affect ZIP size/speed, never the bytes the Wii U extracts.
    """
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


def _version_key(value: str) -> tuple[int, int, int, tuple[int, ...]]:
    match = VERSION_RE.fullmatch(value)
    if not match:
        raise PackageError(f"Invalid version: {value}")
    patches = tuple(int(part) for part in match.group(4).split(".")) if match.group(4) else ()
    return int(match.group(1)), int(match.group(2)), int(match.group(3)), patches


def validate_options(options: BuildOptions) -> None:
    if options.kind not in ("full", "patch"):
        raise PackageError("Package type must be full or patch")
    if options.region not in REGIONS:
        raise PackageError("Package scope must be UNIVERSAL")
    if not VERSION_RE.fullmatch(options.version):
        raise PackageError("Target version must look like 0.0.05 or 0.0.05P1.0")
    if not OWNER_RE.fullmatch(options.github_owner):
        raise PackageError("Enter a valid GitHub owner or organization name")
    if not REPO_RE.fullmatch(options.github_repository):
        raise PackageError("Enter a valid GitHub repository name")
    if options.release_tag and not TAG_RE.fullmatch(options.release_tag):
        raise PackageError("Release tag may contain only letters, numbers, periods, underscores, and hyphens")
    if not options.output_directory.strip():
        raise PackageError("Choose an output directory")
    current = Path(options.current_snapshot).expanduser()
    if not current.is_dir():
        raise PackageError("The current snapshot directory does not exist")
    output = Path(options.output_directory).expanduser()
    if current.resolve() == output.resolve() or current.resolve() in output.resolve().parents:
        raise PackageError("Output directory cannot contain the source snapshot")
    feed = release_feed_path(options)
    if options.release_feed.strip() and feed.name.casefold() != "releases-chain-v2.txt":
        raise PackageError("Select the shared releases-chain-v2.txt feed")
    if feed.exists() and not feed.is_file():
        raise PackageError("The selected release feed is not a file")
    if current.resolve() in feed.resolve().parents:
        raise PackageError("The release feed cannot be stored inside the source snapshot")
    if options.kind == "patch":
        if not VERSION_RE.fullmatch(options.previous_version):
            raise PackageError("Minimum compatible version is required for a patch package")
        if _version_key(options.previous_version) >= _version_key(options.version):
            raise PackageError("Patch target version must be newer than its previous version")
        previous = Path(options.previous_snapshot).expanduser()
        if not previous.is_dir():
            raise PackageError("The minimum-version snapshot directory does not exist")
        if previous.resolve() == current.resolve():
            raise PackageError("Current and previous snapshots must be different directories")


def validate_snapshot_layout(root: str) -> Path:
    """Reject commonly mis-selected or unsupported snapshot directory layouts."""
    base = Path(root).expanduser().resolve()
    if not base.is_dir():
        raise PackageError(f"Snapshot does not exist: {base}")

    ignored_files = {".ds_store", "thumbs.db", "desktop.ini"}
    top_level = [entry for entry in base.iterdir() if entry.name.casefold() not in ignored_files]
    unexpected = [entry.name for entry in top_level if entry.name.casefold() not in {"update", "aoc"}]
    if unexpected:
        sample = ", ".join(sorted(unexpected, key=str.casefold)[:4])
        raise PackageError(
            "The selected snapshot root may contain only update/ (and legacy aoc/). "
            f"Unexpected top-level item(s): {sample}"
        )
    if not any(entry.name.casefold() in {"update", "aoc"} for entry in top_level):
        nested = next((path for path in base.iterdir() if path.is_dir() and (path / "update").is_dir()), None)
        hint = f" Select {nested} instead." if nested else ""
        raise PackageError(f"No update/ snapshot tree was found.{hint}")

    update = base / "update"
    if update.exists():
        if not update.is_dir() or update.is_symlink():
            raise PackageError("update must be a normal directory")
        invalid = [entry.name for entry in update.iterdir()
                   if entry.name.casefold() not in {"code", "content"}]
        if invalid:
            raise PackageError(
                "update/ may contain only code/ and content/. Unexpected item(s): "
                + ", ".join(sorted(invalid, key=str.casefold)[:4])
            )

    legacy_aoc = base / "aoc"
    if legacy_aoc.exists():
        if not legacy_aoc.is_dir() or legacy_aoc.is_symlink():
            raise PackageError("aoc must be a normal directory")
        invalid = [entry.name for entry in legacy_aoc.iterdir()
                   if entry.name.casefold() != "content"]
        if invalid:
            raise PackageError(
                "Legacy aoc/ may contain only content/0010/. Unexpected item(s): "
                + ", ".join(sorted(invalid, key=str.casefold)[:4])
            )
        legacy_content = legacy_aoc / "content"
        if legacy_content.exists():
            invalid = [entry.name for entry in legacy_content.iterdir()
                       if entry.name.casefold() != "0010"]
            if invalid:
                raise PackageError(
                    "Legacy aoc/content/ may contain only 0010/. Unexpected item(s): "
                    + ", ".join(sorted(invalid, key=str.casefold)[:4])
                )
    return base


def scan_snapshot(root: str, callback: Optional[Progress] = None) -> Dict[str, FileRecord]:
    base = validate_snapshot_layout(root)
    candidates: list[tuple[str, Path]] = []
    legacy_aoc_content = base / "aoc" / "content"
    legacy_aoc_0010 = legacy_aoc_content / "0010"
    if legacy_aoc_content.is_dir():
        for legacy_file in legacy_aoc_content.rglob("*"):
            if legacy_file.is_file() and legacy_aoc_0010 not in legacy_file.parents:
                raise PackageError(
                    f"Legacy AOC files must be below aoc/content/0010: {legacy_file}"
                )
    for source_root, archive_root in SNAPSHOT_ROOTS:
        directory = base.joinpath(*source_root.split("/"))
        if not directory.exists():
            continue
        if directory.is_symlink():
            raise PackageError(f"Symlinked directories are not allowed: {directory}")
        for folder, dirs, files in os.walk(directory, followlinks=False):
            folder_path = Path(folder)
            for name in list(dirs):
                if (folder_path / name).is_symlink():
                    raise PackageError(f"Symlinked directories are not allowed: {folder_path / name}")
            for name in files:
                source = folder_path / name
                if source.is_symlink():
                    raise PackageError(f"Symlinked files are not allowed: {source}")
                source_relative = source.relative_to(directory).as_posix()
                relative = f"{archive_root}/{source_relative}"
                if relative == "update/content/update.txt":
                    raise PackageError("Remove update/content/update.txt; the Wii U app owns that version marker")
                parts = relative.split("/")
                if any(part.casefold() in LANGUAGE_FOLDERS for part in parts):
                    raise PackageError(
                        f"Replace the literal language folder with @language in: {relative}"
                    )
                candidates.append((relative, source))
    if not candidates:
        raise PackageError("No files were found under update/code or update/content")
    candidates.sort(key=lambda item: item[0].casefold())
    if len(candidates) > MAX_FILES:
        raise PackageError(f"Snapshot contains {len(candidates)} files; the limit is {MAX_FILES}")
    records: Dict[str, FileRecord] = {}
    folded_paths: set[str] = set()
    total = 0
    for index, (relative, source) in enumerate(candidates, 1):
        folded = relative.casefold()
        if folded in folded_paths:
            raise PackageError(f"Case-colliding archive path: {relative}")
        size = source.stat().st_size
        total = _checked_payload_total(total, size, relative)
        records[relative] = FileRecord(relative, str(source), size, sha256_file(source))
        folded_paths.add(folded)
        _report(callback, f"Hashing {relative}", int(index * 45 / len(candidates)))
    return records


def compare_snapshots(
    previous: Dict[str, FileRecord], current: Dict[str, FileRecord]
) -> tuple[Dict[str, FileRecord], list[str]]:
    included = {
        path: record
        for path, record in current.items()
        if path not in previous or previous[path].sha256 != record.sha256
    }
    deleted = sorted(path for path in previous if path not in current)
    return included, deleted


def _read_catalog(path: Path) -> Optional[dict]:
    if not path.exists():
        return None
    try:
        catalog = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise PackageError(f"Existing release-catalog.json is invalid: {exc}") from exc
    if catalog.get("schemaVersion") != 2:
        raise PackageError("Existing release-catalog.json has an unsupported schema version")
    return catalog


def _valid_catalog_payload_path(value: object) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    parts = value.split("/")
    if len(parts) < 3 or any(part in ("", ".", "..") for part in parts):
        return False
    return parts[:2] in (["update", "code"], ["update", "content"])


def _intermediate_patch_paths(
    catalog: dict, region_name: str, from_version: str, to_version: str
) -> dict[str, list[str]]:
    """Return paths that may differ at any supported intermediate version.

    A FROM->TO package can be applied over every installed version in [FROM, TO).
    The final ZIP must therefore overwrite any file touched while reaching an
    intermediate version, even when that file happens to match the FROM snapshot
    again at TO.
    """
    region = catalog.get("targets", {}).get(region_name)
    if not region:
        return {}
    patches = region.get("patches", [])
    if not isinstance(patches, list):
        raise PackageError("Existing release-catalog.json has an invalid patches collection")

    lower = _version_key(from_version)
    upper = _version_key(to_version)
    touched_by: dict[str, list[str]] = {}
    for item in patches:
        if not isinstance(item, dict):
            raise PackageError("Existing release-catalog.json contains an invalid patch record")
        edge_from = item.get("fromVersion")
        edge_to = item.get("toVersion")
        if not isinstance(edge_from, str) or not isinstance(edge_to, str):
            raise PackageError("Existing release-catalog.json contains a patch without versions")
        try:
            edge_from_key = _version_key(edge_from)
            edge_to_key = _version_key(edge_to)
        except PackageError as exc:
            raise PackageError(
                f"Existing release-catalog.json has invalid patch {edge_from!r} -> {edge_to!r}"
            ) from exc
        if edge_from_key >= edge_to_key:
            raise PackageError(
                f"Existing release-catalog.json has non-increasing patch {edge_from} -> {edge_to}"
            )

        # Ignore the edge being rebuilt. Other edges whose destination is in the
        # requested interval describe a state this minimum-version patch may meet.
        if (edge_from, edge_to) == (from_version, to_version):
            continue
        if not (lower < edge_to_key <= upper and edge_from_key < upper):
            continue

        label = f"{edge_from} -> {edge_to}"
        if item.get("delete"):
            raise PackageError(
                f"Cannot build a safe cumulative patch: catalog patch {label} contains deletions. "
                "Publish a new full release instead."
            )
        file_paths = item.get("filePaths")
        if not isinstance(file_paths, list):
            raise PackageError(
                f"Cannot build a safe cumulative patch: catalog patch {label} lacks filePaths "
                "metadata. Rebuild that patch with this Package Builder or publish a new full release."
            )
        for path in file_paths:
            if not _valid_catalog_payload_path(path):
                raise PackageError(
                    f"Cannot build a safe cumulative patch: catalog patch {label} has an invalid "
                    f"filePaths entry: {path!r}"
                )
            touched_by.setdefault(path, []).append(label)
    return touched_by


def select_patch_files(
    options: BuildOptions,
    previous: Dict[str, FileRecord],
    current: Dict[str, FileRecord],
) -> tuple[Dict[str, FileRecord], list[str]]:
    """Select direct changes plus final copies of historically touched paths."""
    included, deleted = compare_snapshots(previous, current)
    if deleted:
        raise PackageError(
            "This HQ version supports add/replace patches only; restore deleted files or "
            "publish a new full release"
        )

    catalog_path = Path(options.output_directory).expanduser().resolve() / "release-catalog.json"
    catalog = _read_catalog(catalog_path)
    if catalog:
        touched_by = _intermediate_patch_paths(
            catalog, options.region, options.previous_version, options.version
        )
        current_by_case = {path.casefold(): record for path, record in current.items()}
        for historical_path, edges in touched_by.items():
            record = current_by_case.get(historical_path.casefold())
            if record is None:
                raise PackageError(
                    "Cannot build a safe cumulative patch: "
                    f"{historical_path} was touched by catalog patch {edges[0]} but is absent "
                    "from the target snapshot. Publish a new full release instead."
                )
            included[record.path] = record

    if not included:
        raise PackageError("The snapshots are identical; there is no patch to build")
    return included, deleted


def package_asset_name(options: BuildOptions) -> str:
    target = options.region.casefold()
    if options.kind == "full":
        return f"unbound-{target}-{options.version}-full.zip"
    return f"unbound-{target}-{options.previous_version}-to-{options.version}-patch.zip"


def planned_release_record(options: BuildOptions) -> str:
    """Return the feed line shape shown before the archive has been built."""
    archive_name = package_asset_name(options)
    tag = options.release_tag or f"v{options.version}"
    base = f"https://github.com/{options.github_owner}/{options.github_repository}/releases/download/{tag}"
    from_version = options.previous_version if options.kind == "patch" else "-"
    return (
        f"{options.region}|{options.kind.upper()}|{from_version}|{options.version}|"
        f"{base}/{archive_name}|<calculated SHA256>|<calculated bytes>"
    )


def preview_package(options: BuildOptions, callback: Optional[Progress] = None) -> PackagePreview:
    """Validate and hash snapshots without writing a ZIP, catalog, or release feed."""
    validate_options(options)
    _report(callback, "Checking current snapshot", 5)
    current = scan_snapshot(options.current_snapshot, callback)
    included = current
    deleted: list[str] = []
    if options.kind == "patch":
        _report(callback, "Checking previous snapshot", 10)
        previous = scan_snapshot(options.previous_snapshot, callback)
        included, deleted = select_patch_files(options, previous, current)
    archive_name = package_asset_name(options)
    _report(callback, "Preview ready", 100)
    return PackagePreview(
        archive_name=archive_name,
        manifest_name=Path(archive_name).with_suffix(".manifest.json").name,
        release_feed=str(release_feed_path(options)),
        release_record=planned_release_record(options),
        included_paths=tuple(sorted(included, key=str.casefold)),
        deleted_paths=tuple(deleted),
        uncompressed_bytes=sum(record.size for record in included.values()),
    )


def _write_zip(destination: Path, files: Iterable[FileRecord], callback: Optional[Progress]) -> None:
    records = list(files)
    if len(records) > MAX_FILES:
        raise PackageError(f"Package contains {len(records)} files; the limit is {MAX_FILES}")
    total = 0
    for record in records:
        total = _checked_payload_total(total, record.size, record.path)
    temporary = destination.with_suffix(destination.suffix + ".part")
    temporary.unlink(missing_ok=True)
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9, allowZip64=True) as archive:
            for index, record in enumerate(records, 1):
                info = zipfile.ZipInfo(record.path, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = compression_for_file(Path(record.source))
                info.external_attr = 0o100644 << 16
                with Path(record.source).open("rb") as source, archive.open(info, "w", force_zip64=True) as target:
                    shutil.copyfileobj(source, target, length=1024 * 1024)
                _report(callback, f"Packing {record.path}", 45 + int(index * 40 / max(1, len(records))))
        if temporary.stat().st_size >= MAX_ARCHIVE_BYTES:
            raise PackageError(
                "Compressed ZIP must be smaller than 2 GiB "
                f"({MAX_ARCHIVE_BYTES} bytes); split this release into compatible packages"
            )
        os.replace(temporary, destination)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def _write_json_atomic(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def _write_text_atomic(path: Path, value: str) -> None:
    """Write a UTF-8 text file without leaving a half-written release feed."""
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(value)
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def release_feed_path(options: BuildOptions) -> Path:
    """Resolve the shared HQ/game-content feed selected by the publisher."""
    if options.release_feed.strip():
        return Path(options.release_feed).expanduser().resolve()
    return Path(options.output_directory).expanduser().resolve() / "releases-chain-v2.txt"


def release_record_line(options: BuildOptions, archive: Path, digest: str) -> str:
    tag = options.release_tag or f"v{options.version}"
    base = f"https://github.com/{options.github_owner}/{options.github_repository}/releases/download/{tag}"
    from_version = options.previous_version if options.kind == "patch" else "-"
    return (
        f"{options.region}|{options.kind.upper()}|{from_version}|{options.version}|"
        f"{base}/{archive.name}|{digest}|{archive.stat().st_size}"
    )


def _release_identity(line: str) -> Optional[tuple[str, str, str, str]]:
    """Return the identity fields for a valid game-content feed record."""
    if not line or line.lstrip().startswith("#"):
        return None
    fields = line.split("|")
    if len(fields) != 7 or fields[1] not in ("FULL", "PATCH"):
        return None
    return fields[0], fields[1], fields[2], fields[3]


def update_release_feed(path: Path, record_line: str) -> None:
    """Insert or replace one package record while preserving everything else.

    In particular, the shared ``#HQ`` application-update directive, comments,
    blank lines, and records for other package keys are retained byte-for-line.
    Duplicate records with the same identity are collapsed to the new record.
    """
    target_identity = _release_identity(record_line)
    if target_identity is None:
        raise PackageError("Generated release record is invalid")

    if path.exists():
        try:
            lines = path.read_text(encoding="utf-8-sig").splitlines()
        except (OSError, UnicodeError) as exc:
            raise PackageError(f"Could not read the selected release feed: {exc}") from exc
    else:
        lines = []

    if "# UNBOUND_RELEASE_CHAIN_V2" not in lines:
        lines.insert(0, "# UNBOUND_RELEASE_CHAIN_V2")

    package_header = (
        "# PACKAGE_KEY|KIND|FROM_VERSION|TO_VERSION|DOWNLOAD_URL|SHA256|COMPRESSED_BYTES"
    )
    if package_header not in lines:
        first_record = next(
            (index for index, line in enumerate(lines) if _release_identity(line) is not None),
            len(lines),
        )
        lines.insert(first_record, package_header)

    matching = [
        index for index, line in enumerate(lines) if _release_identity(line) == target_identity
    ]
    if matching:
        first = matching[0]
        lines[first] = record_line
        for index in reversed(matching[1:]):
            del lines[index]
    else:
        package_records = [
            index for index, line in enumerate(lines) if _release_identity(line) is not None
        ]
        if package_records:
            insert_at = package_records[-1] + 1
        else:
            insert_at = lines.index(package_header) + 1
        lines.insert(insert_at, record_line)

    try:
        _write_text_atomic(path, "\n".join(lines).rstrip("\n") + "\n")
    except OSError as exc:
        raise PackageError(f"Could not update the selected release feed: {exc}") from exc


def _record_for_catalog(options: BuildOptions, archive: Path, digest: str, manifest_name: str,
                        records: Dict[str, FileRecord], deleted: list[str]) -> dict:
    tag = options.release_tag or f"v{options.version}"
    base = f"https://github.com/{options.github_owner}/{options.github_repository}/releases/download/{tag}"
    return {
        "kind": options.kind,
        "fromVersion": options.previous_version if options.kind == "patch" else None,
        "toVersion": options.version,
        "tag": tag,
        "assetName": archive.name,
        "downloadUrl": f"{base}/{archive.name}",
        "sha256": digest,
        "byteSize": archive.stat().st_size,
        "uncompressedBytes": sum(record.size for record in records.values()),
        "fileCount": len(records),
        "filePaths": sorted(records, key=str.casefold),
        "requiresAoc": False,
        "delete": deleted,
        "packageManifestUrl": f"{base}/{manifest_name}",
    }


def _merge_catalog(path: Path, options: BuildOptions, record: dict) -> dict:
    catalog = _read_catalog(path)
    if catalog is None:
        catalog = {"schemaVersion": 2, "repository": {}, "targets": {}}
    catalog["generatedAt"] = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    catalog["repository"] = {"owner": options.github_owner, "name": options.github_repository}
    region = catalog.setdefault("targets", {}).setdefault(
        options.region, {"latestVersion": options.version, "fullPackages": [], "patches": []}
    )
    collection_name = "fullPackages" if options.kind == "full" else "patches"
    collection = region.setdefault(collection_name, [])
    identity = (record["fromVersion"], record["toVersion"])
    collection[:] = [item for item in collection
                     if (item.get("fromVersion"), item.get("toVersion")) != identity]
    collection.append(record)
    collection.sort(key=lambda item: (_version_key(item["toVersion"]), item.get("fromVersion") or ""))
    all_versions = [item["toVersion"] for key in ("fullPackages", "patches")
                    for item in region.get(key, [])]
    region["latestVersion"] = max(all_versions, key=_version_key)
    _write_json_atomic(path, catalog)
    return catalog


def build_package(options: BuildOptions, callback: Optional[Progress] = None) -> BuildResult:
    validate_options(options)
    _report(callback, "Scanning current snapshot", 1)
    current = scan_snapshot(options.current_snapshot, callback)
    deleted: list[str] = []
    included = current
    if options.kind == "patch":
        _report(callback, "Scanning previous snapshot", 5)
        previous = scan_snapshot(options.previous_snapshot, callback)
        included, deleted = select_patch_files(options, previous, current)
    output = Path(options.output_directory).expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / package_asset_name(options)
    _write_zip(archive, included.values(), callback)
    _report(callback, "Calculating package SHA-256", 88)
    digest = sha256_file(archive)
    manifest_path = archive.with_suffix(".manifest.json")
    record = _record_for_catalog(options, archive, digest, manifest_path.name, included, deleted)
    package_manifest = {
        "schemaVersion": 2,
        "createdAt": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "packageKey": options.region,
        **record,
        "files": [asdict(record_value) | {"source": None} for record_value in included.values()],
    }
    for file_entry in package_manifest["files"]:
        file_entry.pop("source", None)
    _write_json_atomic(manifest_path, package_manifest)
    _report(callback, "Updating release catalog", 94)
    catalog_path = output / "release-catalog.json"
    catalog = _merge_catalog(catalog_path, options, record)
    chain_path = release_feed_path(options)
    feed_record = release_record_line(options, archive, digest)
    _report(callback, "Updating shared release feed", 97)
    feed_updated = True
    feed_error = ""
    try:
        update_release_feed(chain_path, feed_record)
    except (PackageError, OSError) as exc:
        # The archive and metadata are still valid. Report this as a recoverable
        # publishing warning instead of pretending the entire build disappeared.
        feed_updated = False
        feed_error = str(exc)
    _report(callback, "Package complete", 100)
    return BuildResult(
        str(archive), str(manifest_path), str(catalog_path), str(chain_path), feed_record, digest,
        len(included), len(deleted), archive.stat().st_size,
        sum(record_value.size for record_value in included.values()),
        feed_updated, feed_error,
    )


def create_snapshot_template(destination: str) -> Path:
    root = Path(destination).expanduser().resolve()
    root.joinpath("update", "code").mkdir(parents=True, exist_ok=True)
    root.joinpath("update", "content").mkdir(parents=True, exist_ok=True)
    root.joinpath("update", "content", "@language").mkdir(parents=True, exist_ok=True)
    return root
