from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import stage_split_release
from stage_split_release import MAX_FILES, collect, main, merge_files, write_archive


class SplitReleaseTests(unittest.TestCase):
    def test_expanded_limit_keeps_file_and_asset_limits_separate(self) -> None:
        self.assertEqual(stage_split_release.MAX_UNCOMPRESSED_BYTES, 5 * 1024 ** 3)
        self.assertEqual(stage_split_release.MAX_FILE_BYTES, 2 * 1024 ** 3)
        self.assertEqual(stage_split_release.GITHUB_ASSET_LIMIT, 2 * 1024 ** 3)

    def test_archive_accepts_exact_expanded_limit_and_rejects_overflow(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sources = [root / name for name in ("a.bin", "b.bin", "c.bin")]
            for source, value in zip(sources, (b"1234", b"1234", b"12")):
                source.write_bytes(value)
            files = [(source, f"update/content/{source.name}") for source in sources]
            with patch("stage_split_release.MAX_FILE_BYTES", 4), patch("stage_split_release.MAX_UNCOMPRESSED_BYTES", 10):
                write_archive(root / "exact.zip", files)
                self.assertTrue((root / "exact.zip").exists())
                sources[-1].write_bytes(b"123")
                with self.assertRaisesRegex(RuntimeError, "5 GiB.*total uncompressed"):
                    write_archive(root / "too-large.zip", files)
                self.assertFalse((root / "too-large.zip").exists())
                sources[0].write_bytes(b"12345")
                with self.assertRaisesRegex(RuntimeError, "per-file limit"):
                    write_archive(root / "one-file-too-large.zip", files)

    def test_compressed_limit_is_checked_before_replacing_existing_archive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "one.bin"
            source.write_bytes(b"one")
            destination = root / "release.zip"
            destination.write_bytes(b"preserve previous archive")
            with patch("stage_split_release.GITHUB_ASSET_LIMIT", 1):
                with self.assertRaisesRegex(RuntimeError, "2 GiB compressed ZIP limit"):
                    write_archive(destination, [(source, "update/content/one.bin")])
            self.assertEqual(destination.read_bytes(), b"preserve previous archive")
            self.assertFalse(destination.with_suffix(".zip.part").exists())

    def test_language_mapping_rejects_duplicate_archive_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            english = root / "english" / "patch.ff"
            placeholder = root / "@language" / "patch.ff"
            english.parent.mkdir(parents=True)
            placeholder.parent.mkdir(parents=True)
            english.write_bytes(b"english")
            placeholder.write_bytes(b"placeholder")

            with self.assertRaisesRegex(RuntimeError, "More than one source file maps"):
                collect(root, "update/content", "english")

    def test_legacy_aoc_and_update_collision_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            update = root / "update.bin"
            aoc = root / "aoc.bin"
            update.write_bytes(b"update")
            aoc.write_bytes(b"aoc")
            with self.assertRaisesRegex(RuntimeError, "More than one source file maps"):
                merge_files(
                    [(update, "update/content/0010/map.ipak")],
                    [(aoc, "update/content/0010/MAP.IPAK")],
                )

    def test_merged_archive_rejects_more_files_than_hq_accepts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "one.bin"
            source.write_bytes(b"1")
            files = [(source, f"update/content/{index}.bin") for index in range(MAX_FILES + 1)]
            with self.assertRaisesRegex(RuntimeError, "HQ accepts at most"):
                write_archive(root / "too-many.zip", files)

    def test_cli_builds_one_update_only_release(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            output = root / "output"
            update_code = source / "0005000e" / "1010cf00" / "code"
            update_content = source / "0005000e" / "1010cf00" / "content" / "english"
            aoc_content = source / "0005000c" / "1010cf00" / "content" / "0010" / "english"
            update_code.mkdir(parents=True)
            update_content.mkdir(parents=True)
            aoc_content.mkdir(parents=True)
            (update_code / "game.rpl").write_bytes(b"code")
            (update_content / "core.ff").write_bytes(b"core")
            (aoc_content / "map.ff").write_bytes(b"map")
            arguments = [
                "stage_split_release.py", str(source), str(output),
                "--version", "0.0.05", "--tag", "v0.0.05",
                "--owner", "UnboundTeam",
            ]
            with patch.object(sys, "argv", arguments):
                self.assertEqual(main(), 0)
            archives = list(output.glob("*.zip"))
            self.assertEqual([archive.name for archive in archives],
                             ["unbound-universal-0.0.05-full.zip"])
            with zipfile.ZipFile(archives[0]) as archive:
                self.assertEqual(
                    archive.namelist(),
                    [
                        "update/code/game.rpl",
                        "update/content/@language/core.ff",
                        "update/content/@language/map.ff",
                    ],
                )
            manifest = json.loads(
                (output / "unbound-universal-0.0.05.manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(len(manifest["components"]), 1)
            feed = (output / "releases-update-only.txt").read_text(encoding="utf-8")
            record = next(line for line in feed.splitlines() if line and not line.startswith("#"))
            self.assertEqual(len(record.split("|")), 4)
            self.assertEqual(len((output / "checksums.sha256").read_text().splitlines()), 2)


if __name__ == "__main__":
    unittest.main()
