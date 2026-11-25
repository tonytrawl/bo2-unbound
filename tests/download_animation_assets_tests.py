"""Verify the packaged atlas pixels and timings against the original GIFs."""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


def verify(source_dir: Path, project: Path) -> None:
    assets = project / "content" / "animations"
    manifest = json.loads((assets / "manifest.json").read_text(encoding="utf-8"))
    sheets = [Image.open(assets / item["file"]).convert("RGBA") for item in manifest["atlases"]]
    decoded_bytes = 0
    for sheet, metadata in zip(sheets, manifest["atlases"]):
        assert sheet.size == (metadata["width"], metadata["height"])
        assert 0 < sheet.width <= 1024 and 0 < sheet.height <= 1024
        decoded_bytes += sheet.width * sheet.height * 4
    assert decoded_bytes == manifest["atlas_rgba_bytes"]
    assert decoded_bytes + 1280 * 720 * 4 == manifest["total_rgba_bytes"]
    assert manifest["total_rgba_bytes"] <= 12 * 1024 * 1024
    verified_frames = 0
    for clip in manifest["clips"]:
        source = source_dir / clip["source"]
        assert hashlib.sha256(source.read_bytes()).hexdigest() == clip["source_sha256"]
        with Image.open(source) as original:
            assert original.size == (clip["source_width"], clip["source_height"])
            assert original.n_frames == clip["frame_count"] == len(clip["frames"])
            assert original.info.get("loop", 1) == clip["source_loop"]
            elapsed = 0
            for index, (atlas, x, y, width, height, end_ms) in enumerate(clip["frames"]):
                original.seek(index)
                elapsed += original.info["duration"]
                assert elapsed == end_ms
                decoded = original.convert("RGBA").resize((width, height), Image.Resampling.LANCZOS)
                packed = sheets[atlas].crop((x, y, x + width, y + height))
                # Exact RGBA comparison catches altered backgrounds/alpha,
                # misplaced cells, GIF disposal mistakes and dropped frames.
                assert packed.tobytes() == decoded.tobytes(), (clip["source"], index)
                verified_frames += 1
            assert elapsed == clip["duration_ms"]
    for sheet in sheets:
        sheet.close()
    print(f"Verified {verified_frames} original frames, exact RGBA pixels, loop metadata and all frame durations")
    print(f"Combined sprite/cache budget: {manifest['total_rgba_bytes']:,} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parent.parent)
    arguments = parser.parse_args()
    verify(arguments.source, arguments.project)
