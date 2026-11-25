from __future__ import annotations

import json
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import packager_core
from packager_core import (
    BuildOptions,
    PackageError,
    build_package,
    create_snapshot_template,
    preview_package,
    update_release_feed,
)


class PackageBuilderTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.old = create_snapshot_template(str(self.root / "old"))
        self.new = create_snapshot_template(str(self.root / "new"))
        self.output = self.root / "out"

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write(self, root: Path, relative: str, value: bytes) -> None:
        path = root.joinpath(*relative.split("/"))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value)

    def options(self, kind: str, version: str, previous_version: str = "") -> BuildOptions:
        return BuildOptions(
            kind=kind,
            region="UNIVERSAL",
            version=version,
            current_snapshot=str(self.new),
            previous_snapshot=str(self.old),
            previous_version=previous_version,
            output_directory=str(self.output),
            github_owner="UnboundTeam",
            github_repository="bo2-unbound",
            release_tag=f"v{version}",
        )

    def test_full_package_and_shared_release_feed(self) -> None:
        self.write(self.new, "update/code/default.rpx", b"code")
        self.write(self.new, "update/content/mods/Unbound/config.txt", b"content")
        result = build_package(self.options("full", "1.0.0"))
        with zipfile.ZipFile(result.archive) as archive:
            self.assertEqual(
                archive.namelist(),
                ["update/code/default.rpx", "update/content/mods/Unbound/config.txt"],
            )
        manifest = json.loads(Path(result.package_manifest).read_text(encoding="utf-8"))
        self.assertEqual(manifest["kind"], "full")
        self.assertEqual(manifest["fileCount"], 2)
        catalog = json.loads(Path(result.catalog).read_text(encoding="utf-8"))
        full_record = catalog["targets"]["UNIVERSAL"]["fullPackages"][0]
        self.assertEqual(
            full_record["filePaths"],
            ["update/code/default.rpx", "update/content/mods/Unbound/config.txt"],
        )
        chain = Path(result.chain_releases).read_text(encoding="utf-8")
        self.assertIn("UNIVERSAL|FULL|-|1.0.0|", chain)
        self.assertIn(result.sha256, chain)
        self.assertEqual(len(next(line for line in chain.splitlines()
                                  if line and not line.startswith("#")).split("|")), 7)
        self.assertTrue(result.release_feed_updated)
        self.assertEqual(result.release_feed_error, "")

    def test_template_contains_only_update_destinations(self) -> None:
        self.assertTrue((self.new / "update" / "code").is_dir())
        self.assertTrue((self.new / "update" / "content" / "@language").is_dir())
        self.assertFalse((self.new / "aoc").exists())

    def test_expanded_limit_is_5_gib_without_raising_file_or_download_limits(self) -> None:
        gib = 1024 ** 3
        self.assertEqual(packager_core.MAX_UNCOMPRESSED_BYTES, 5 * gib)
        self.assertEqual(packager_core.MAX_FILE_BYTES, 2 * gib)
        self.assertEqual(packager_core.MAX_ARCHIVE_BYTES, 2 * gib)
        self.assertEqual(packager_core._checked_payload_total(3 * gib, 2 * gib, "last.bin"), 5 * gib)
        self.assertEqual(
            packager_core._checked_payload_total(2 * gib, 8064802, "v0.2.0-last.bin"),
            2155548450,
        )
        with self.assertRaisesRegex(PackageError, "5 GiB total"):
            packager_core._checked_payload_total(5 * gib, 1, "over.bin")
        with self.assertRaisesRegex(PackageError, "per-file limit"):
            packager_core._checked_payload_total(0, 2 * gib + 1, "huge.bin")

    def test_scan_enforces_individual_and_combined_size_before_build(self) -> None:
        self.write(self.new, "update/code/a.bin", b"1234")
        self.write(self.new, "update/content/b.bin", b"1234")
        self.write(self.new, "update/content/c.bin", b"12")
        with patch("packager_core.MAX_FILE_BYTES", 4), patch("packager_core.MAX_UNCOMPRESSED_BYTES", 10):
            self.assertEqual(preview_package(self.options("full", "1.0.0")).uncompressed_bytes, 10)
            self.write(self.new, "update/content/c.bin", b"123")
            with self.assertRaisesRegex(PackageError, "5 GiB total"):
                preview_package(self.options("full", "1.0.0"))
            self.write(self.new, "update/code/a.bin", b"12345")
            with self.assertRaisesRegex(PackageError, "per-file limit"):
                preview_package(self.options("full", "1.0.0"))
        self.assertFalse(self.output.exists())

    def test_oversized_compressed_archive_is_not_published(self) -> None:
        self.write(self.new, "update/code/game.rpl", b"game")
        self.output.mkdir()
        destination = self.output / "unbound-universal-1.0.0-full.zip"
        destination.write_bytes(b"preserve previous archive")
        with patch("packager_core.MAX_ARCHIVE_BYTES", 1):
            with self.assertRaisesRegex(PackageError, "Compressed ZIP must be smaller than 2 GiB"):
                build_package(self.options("full", "1.0.0"))
        self.assertEqual(destination.read_bytes(), b"preserve previous archive")
        self.assertFalse(destination.with_suffix(".zip.part").exists())
        self.assertFalse((self.output / "releases-chain-v2.txt").exists())
        self.assertFalse(destination.with_suffix(".manifest.json").exists())

    def test_patch_contains_only_changes_and_extends_chain(self) -> None:
        self.write(self.old, "update/code/default.rpx", b"old")
        self.write(self.old, "update/content/0010/same.bin", b"same")
        base_options = self.options("full", "1.0.0")
        base_options = BuildOptions(**{**base_options.__dict__, "current_snapshot": str(self.old)})
        build_package(base_options)
        self.write(self.new, "update/code/default.rpx", b"new")
        self.write(self.new, "update/content/new.bin", b"new file")
        self.write(self.new, "update/content/0010/same.bin", b"same")
        result = build_package(self.options("patch", "1.0.0P1.0", "1.0.0"))
        with zipfile.ZipFile(result.archive) as archive:
            self.assertEqual(archive.namelist(), ["update/code/default.rpx", "update/content/new.bin"])
        manifest = json.loads(Path(result.package_manifest).read_text(encoding="utf-8"))
        self.assertEqual(manifest["fromVersion"], "1.0.0")
        self.assertEqual(manifest["toVersion"], "1.0.0P1.0")
        self.assertEqual(manifest["delete"], [])
        self.assertEqual(result.included_files, 2)
        self.assertEqual(result.deleted_files, 0)
        catalog = json.loads(Path(result.catalog).read_text(encoding="utf-8"))
        patch_record = catalog["targets"]["UNIVERSAL"]["patches"][0]
        self.assertEqual(
            patch_record["filePaths"],
            ["update/code/default.rpx", "update/content/new.bin"],
        )
        chain = Path(result.chain_releases).read_text(encoding="utf-8")
        self.assertIn("UNIVERSAL|FULL|-|1.0.0|", chain)
        self.assertIn("UNIVERSAL|PATCH|1.0.0|1.0.0P1.0|", chain)

    def test_cumulative_patch_includes_final_copy_of_reverted_intermediate_file(self) -> None:
        base = create_snapshot_template(str(self.root / "base"))
        patch_one = create_snapshot_template(str(self.root / "patch-one"))
        patch_two = create_snapshot_template(str(self.root / "patch-two"))
        path = "update/code/default.rpx"
        self.write(base, path, b"base bytes")
        self.write(patch_one, path, b"temporarily changed")
        self.write(patch_two, path, b"base bytes")

        base_options = self.options("full", "1.0.0")
        build_package(BuildOptions(**{**base_options.__dict__, "current_snapshot": str(base)}))

        patch_one_options = self.options("patch", "1.0.0P1.0", "1.0.0")
        patch_one_options = BuildOptions(**{
            **patch_one_options.__dict__,
            "previous_snapshot": str(base),
            "current_snapshot": str(patch_one),
        })
        build_package(patch_one_options)

        patch_two_options = self.options("patch", "1.0.0P2.0", "1.0.0P1.0")
        patch_two_options = BuildOptions(**{
            **patch_two_options.__dict__,
            "previous_snapshot": str(patch_one),
            "current_snapshot": str(patch_two),
        })
        build_package(patch_two_options)

        cumulative_options = self.options("patch", "1.0.0P2.0", "1.0.0")
        cumulative_options = BuildOptions(**{
            **cumulative_options.__dict__,
            "previous_snapshot": str(base),
            "current_snapshot": str(patch_two),
        })
        result = build_package(cumulative_options)
        with zipfile.ZipFile(result.archive) as archive:
            self.assertEqual(archive.namelist(), [path])
            self.assertEqual(archive.read(path), b"base bytes")

    def test_cumulative_patch_rejects_intermediate_record_without_file_paths(self) -> None:
        base = create_snapshot_template(str(self.root / "base"))
        patch_one = create_snapshot_template(str(self.root / "patch-one"))
        patch_two = create_snapshot_template(str(self.root / "patch-two"))
        self.write(base, "update/code/default.rpx", b"base")
        self.write(patch_one, "update/code/default.rpx", b"one")
        self.write(patch_two, "update/code/default.rpx", b"two")

        base_options = self.options("full", "1.0.0")
        build_package(BuildOptions(**{**base_options.__dict__, "current_snapshot": str(base)}))
        patch_one_options = self.options("patch", "1.0.0P1.0", "1.0.0")
        patch_one_options = BuildOptions(**{
            **patch_one_options.__dict__,
            "previous_snapshot": str(base),
            "current_snapshot": str(patch_one),
        })
        first_result = build_package(patch_one_options)
        catalog_path = Path(first_result.catalog)
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        del catalog["targets"]["UNIVERSAL"]["patches"][0]["filePaths"]
        catalog_path.write_text(json.dumps(catalog), encoding="utf-8")

        cumulative_options = self.options("patch", "1.0.0P2.0", "1.0.0")
        cumulative_options = BuildOptions(**{
            **cumulative_options.__dict__,
            "previous_snapshot": str(base),
            "current_snapshot": str(patch_two),
        })
        with self.assertRaisesRegex(PackageError, "lacks filePaths metadata"):
            build_package(cumulative_options)

    def test_cumulative_patch_rejects_touched_path_missing_from_target(self) -> None:
        base = create_snapshot_template(str(self.root / "base"))
        patch_one = create_snapshot_template(str(self.root / "patch-one"))
        patch_two = create_snapshot_template(str(self.root / "patch-two"))
        self.write(base, "update/code/anchor.rpx", b"same")
        self.write(patch_one, "update/code/anchor.rpx", b"same")
        self.write(patch_one, "update/content/transient.bin", b"temporary")
        self.write(patch_two, "update/code/anchor.rpx", b"same")

        base_options = self.options("full", "1.0.0")
        build_package(BuildOptions(**{**base_options.__dict__, "current_snapshot": str(base)}))
        patch_one_options = self.options("patch", "1.0.0P1.0", "1.0.0")
        patch_one_options = BuildOptions(**{
            **patch_one_options.__dict__,
            "previous_snapshot": str(base),
            "current_snapshot": str(patch_one),
        })
        build_package(patch_one_options)

        cumulative_options = self.options("patch", "1.0.0P2.0", "1.0.0")
        cumulative_options = BuildOptions(**{
            **cumulative_options.__dict__,
            "previous_snapshot": str(base),
            "current_snapshot": str(patch_two),
        })
        with self.assertRaisesRegex(PackageError, "absent from the target snapshot"):
            build_package(cumulative_options)

    def test_patch_deletions_are_rejected_until_runtime_rollback_support_exists(self) -> None:
        self.write(self.old, "update/code/default.rpx", b"same")
        self.write(self.new, "update/code/default.rpx", b"same")
        self.write(self.old, "update/content/remove-me.bin", b"remove")
        with self.assertRaisesRegex(PackageError, "add/replace patches only"):
            build_package(self.options("patch", "1.0.0P1.0", "1.0.0"))

    def test_version_marker_is_rejected(self) -> None:
        self.write(self.new, "update/content/update.txt", b"1.0.0")
        with self.assertRaises(PackageError):
            build_package(self.options("full", "1.0.0"))

    def test_literal_language_folder_is_rejected(self) -> None:
        self.write(self.new, "update/content/english/patch.ff", b"localized")
        with self.assertRaises(PackageError):
            build_package(self.options("full", "1.0.0"))

    def test_legacy_aoc_snapshot_is_migrated_into_update_content(self) -> None:
        self.write(self.new, "aoc/content/0010/@language/map.ff", b"map")
        result = build_package(self.options("full", "1.0.0"))
        with zipfile.ZipFile(result.archive) as archive:
            self.assertEqual(archive.namelist(), ["update/content/@language/map.ff"])
        manifest = json.loads(Path(result.package_manifest).read_text(encoding="utf-8"))
        self.assertFalse(manifest["requiresAoc"])

    def test_legacy_aoc_collision_with_update_is_rejected(self) -> None:
        self.write(self.new, "update/content/map.ipak", b"update")
        self.write(self.new, "aoc/content/0010/map.ipak", b"aoc")
        with self.assertRaisesRegex(PackageError, "Case-colliding archive path"):
            build_package(self.options("full", "1.0.0"))

    def test_legacy_aoc_file_outside_0010_is_rejected(self) -> None:
        self.write(self.new, "aoc/content/unexpected.bin", b"bad")
        with self.assertRaisesRegex(PackageError, "may contain only 0010"):
            build_package(self.options("full", "1.0.0"))

    def test_patch_level_version_is_supported(self) -> None:
        self.write(self.old, "update/code/default.rpx", b"base")
        self.write(self.new, "update/code/default.rpx", b"patch")
        result = build_package(self.options("patch", "1.0.0P1.0", "1.0.0"))
        self.assertEqual(result.included_files, 1)

    def test_adaptive_compression_preserves_bytes(self) -> None:
        compressible = b"A" * (512 * 1024)
        incompressible = b"".join(
            hashlib.sha256(index.to_bytes(4, "big")).digest() for index in range(16384)
        )
        self.write(self.new, "update/content/compressible.bin", compressible)
        self.write(self.new, "update/content/already-packed.bin", incompressible)
        result = build_package(self.options("full", "1.0.0"))
        with zipfile.ZipFile(result.archive) as archive:
            self.assertEqual(archive.read("update/content/compressible.bin"), compressible)
            self.assertEqual(archive.read("update/content/already-packed.bin"), incompressible)
            self.assertEqual(
                archive.getinfo("update/content/compressible.bin").compress_type,
                zipfile.ZIP_DEFLATED,
            )
            self.assertEqual(
                archive.getinfo("update/content/already-packed.bin").compress_type,
                zipfile.ZIP_STORED,
            )

    def test_preview_has_exact_asset_and_feed_shape_without_writing_outputs(self) -> None:
        self.write(self.new, "update/code/game.rpl", b"code")
        options = self.options("full", "0.0.05")
        preview = preview_package(options)
        self.assertEqual(preview.archive_name, "unbound-universal-0.0.05-full.zip")
        self.assertEqual(preview.included_paths, ("update/code/game.rpl",))
        self.assertIn("/releases/download/v0.0.05/unbound-universal-0.0.05-full.zip", preview.release_record)
        self.assertTrue(preview.release_record.endswith("|<calculated SHA256>|<calculated bytes>"))
        self.assertFalse(self.output.exists())

    def test_shared_feed_update_preserves_hq_comments_and_unrelated_records(self) -> None:
        feed = self.root / "releases-chain-v2.txt"
        original = (
            "# UNBOUND_RELEASE_CHAIN_V2\n"
            "# keep this publisher note\n"
            "#HQ|7|0.0.07-beta|https://example.invalid/hq.wuhb|abc|123\n"
            "OTHER|FULL|-|9.0.0|https://example.invalid/other.zip|def|456\n"
            "UNIVERSAL|FULL|-|1.0.0|https://old.invalid/file.zip|old|12\n"
        )
        feed.write_text(original, encoding="utf-8")
        replacement = "UNIVERSAL|FULL|-|1.0.0|https://new.invalid/file.zip|new|34"
        update_release_feed(feed, replacement)
        updated = feed.read_text(encoding="utf-8")
        self.assertIn("# keep this publisher note", updated)
        self.assertIn("#HQ|7|0.0.07-beta|", updated)
        self.assertIn("OTHER|FULL|-|9.0.0|", updated)
        self.assertIn(replacement, updated)
        self.assertNotIn("https://old.invalid/file.zip", updated)

    def test_build_reports_feed_failure_without_hiding_created_package(self) -> None:
        self.write(self.new, "update/code/game.rpl", b"code")
        with patch("packager_core.update_release_feed", side_effect=PackageError("feed is read-only")):
            result = build_package(self.options("full", "1.0.0"))
        self.assertTrue(Path(result.archive).is_file())
        self.assertTrue(Path(result.package_manifest).is_file())
        self.assertFalse(result.release_feed_updated)
        self.assertEqual(result.release_feed_error, "feed is read-only")

    def test_snapshot_rejects_unexpected_top_level_items(self) -> None:
        self.write(self.new, "update/code/game.rpl", b"code")
        self.write(self.new, "wrong/content.bin", b"bad")
        with self.assertRaisesRegex(PackageError, "Unexpected top-level"):
            preview_package(self.options("full", "1.0.0"))

    def test_catalog_uses_same_v_version_default_tag_as_feed(self) -> None:
        self.write(self.new, "update/code/game.rpl", b"code")
        options = self.options("full", "1.0.0")
        options = BuildOptions(**{**options.__dict__, "release_tag": ""})
        result = build_package(options)
        catalog = json.loads(Path(result.catalog).read_text(encoding="utf-8"))
        record = catalog["targets"]["UNIVERSAL"]["fullPackages"][0]
        self.assertEqual(record["tag"], "v1.0.0")
        self.assertIn("/releases/download/v1.0.0/", record["downloadUrl"])

    def test_invalid_release_tag_is_rejected_before_build(self) -> None:
        self.write(self.new, "update/code/game.rpl", b"code")
        options = self.options("full", "1.0.0")
        options = BuildOptions(**{**options.__dict__, "release_tag": "v1.0.0 beta"})
        with self.assertRaisesRegex(PackageError, "Release tag"):
            preview_package(options)


if __name__ == "__main__":
    unittest.main()
