"""Stage only runtime content; WUHB menu metadata is packaged separately.

Keep source artwork and retired animation sheets in the repository, outside the
shipping content filesystem. Never alter or recompress the original assets.
"""
from pathlib import Path
import shutil


def prepare(root: Path) -> tuple[int, int]:
    root = root.resolve()
    source = root / "content"
    destination = root / ".package-content"
    # This fixed, checked path is the only directory this script may prune.
    if destination.is_symlink() or destination.resolve().parent != root:
        raise ValueError("Runtime content staging must stay inside the project")
    excluded_assets = {"menu_icon_source.png", "Gamepad_Screen.png", "bo2_unbound_logo.png"}
    files = {}
    for item in source.rglob("*"):
        if not item.is_file():
            continue
        relative = item.relative_to(source)
        if relative.parts[0] in {"meta", "animations"}:
            continue
        if relative.parts[:3] == ("assets", "icons", "lucide-source"):
            continue
        if relative.parts[0] == "assets" and item.name in excluded_assets:
            continue
        files[relative] = item
    destination.mkdir(exist_ok=True)
    for old in destination.rglob("*"):
        if old.is_symlink():
            raise ValueError("Runtime content staging cannot contain symlinks")
        if old.is_file() and old.relative_to(destination) not in files:
            old.unlink()
    for relative, item in files.items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != item.read_bytes():
            shutil.copyfile(item, target)
    runtime_bytes = sum(item.stat().st_size for item in files.values())
    source_bytes = sum(item.stat().st_size for item in source.rglob("*") if item.is_file())
    return runtime_bytes, source_bytes - runtime_bytes


if __name__ == "__main__":
    included, excluded = prepare(Path(__file__).resolve().parents[1])
    print(f"Runtime content: {included:,} bytes; omitted from content: {excluded:,} bytes")
