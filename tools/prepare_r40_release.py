"""Stage the existing r40 binary and a publisher handoff; never publish or commit."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request
import zipfile

project = Path(__file__).resolve().parents[1]
source = project / "staging-r40/BO2-Unbound-HQ-v0.0.40-beta-TEST"
destination = project / "release-r40"
receipt = json.loads((source / "BUILD_RECEIPT.json").read_text(encoding="utf-8"))

def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

assert receipt["revision"] == 40 and receipt["version"] == "0.0.40-beta"
assert sha(source / "BO2-Unbound-HQ.wuhb") == receipt["wuhbSha256"]
assert (source / "BO2-Unbound-HQ.wuhb").stat().st_size == receipt["wuhbBytes"]
for name, expected in receipt["sourceSha256"].items():
    assert sha(project / name) == expected, name

feed_url = "https://raw.githubusercontent.com/tonytrawl/bo2-unbound/main/releases-chain-v2.txt"
with urllib.request.urlopen(feed_url, timeout=25) as response:
    live_feed = response.read().decode("utf-8-sig")
assert live_feed.startswith("# UNBOUND_RELEASE_CHAIN_V2")
assert len([line for line in live_feed.splitlines() if line.startswith("#HQ|")]) == 1
tag = "hq-v0.0.40-beta"
record = (f"#HQ|40|0.0.40-beta|https://github.com/tonytrawl/bo2-unbound/releases/download/{tag}/"
          f"BO2-Unbound-HQ.wuhb|{receipt['wuhbSha256']}|{receipt['wuhbBytes']}")
lines = live_feed.splitlines()
staged_feed = "\n".join(record if line.startswith("#HQ|") else line for line in lines) + "\n"
assert [line for line in staged_feed.splitlines() if not line.startswith("#HQ|")] == [
    line for line in lines if not line.startswith("#HQ|")]

destination.mkdir(exist_ok=False)
def write(name, text):
    with (destination / name).open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(text)

shutil.copy2(source / "BO2-Unbound-HQ.wuhb", destination / "BO2-Unbound-HQ.wuhb")
archive = destination / "BO2-Unbound-HQ-v0.0.40-beta-sd.zip"
shutil.copy2(source / "BO2-Unbound-HQ-v0.0.40-beta-TEST-sd.zip", archive)
with zipfile.ZipFile(archive) as packed:
    assert packed.namelist() == ["wiiu/apps/BO2-Unbound-HQ/BO2-Unbound-HQ.wuhb"]
    assert packed.testzip() is None
    assert hashlib.sha256(packed.read(packed.namelist()[0])).hexdigest() == receipt["wuhbSha256"]
write("hq-release-record.txt", record + "\n")
write("releases-chain-v2.txt", staged_feed)
write("RELEASE_NOTES.txt", """## Unbound HQ 0.0.40-beta

- Fixes installation of added loading movies using the game's existing localized movie folder.
- Leaves the shared stock movie library in place instead of duplicating it.
- Keeps installation recovery, progress reporting and full download verification.
- Reuses a complete, valid saved game download after verification, so an interrupted install does not automatically require another download.
- Includes the official HQ theme song and the updated download screen.

Update HQ, relaunch, then open Install / Update to resume your installation.
For manual installation, extract the SD ZIP onto your SD card, keeping your settings and cache.
""")
write("DISCORD_ANNOUNCEMENT.txt", """## 🛠️ HQ INSTALL FIX IS HERE!

One sleepless night later, we’ve beaten the final boss: folders.

Some Wii U update folders reference the original game files, and HQ was tripping over them. HQ now handles the required folders correctly and puts added loading movies where the game already looks for them, leaving the stock movie library alone.

**Open HQ, accept the update, then relaunch and resume your installation.**

HQ will scan your saved download to confirm it’s complete and valid. **If it checks out, you won’t need another huge download.**

🎵 The **official HQ theme song** is included too. Even the progress bar deserves a soundtrack.

Sorry for the inconvenience, and thanks for sticking with us. **Have an amazing Beta 3!** 🔥
""")
write("UPLOAD.txt", f"""BLACK OPS 2 UNBOUND HQ 0.0.40-beta / r40

1. Create a GitHub release in tonytrawl/bo2-unbound.
   Tag: {tag}
   Title: Unbound HQ 0.0.40-beta
2. Attach BO2-Unbound-HQ.wuhb with that exact filename. This is the self-updater's asset.
   Optionally attach BO2-Unbound-HQ-v0.0.40-beta-sd.zip for manual SD installation.
   Use RELEASE_NOTES.txt as the release description.
3. Publish the release and confirm its WUHB asset downloads before changing the feed.
4. At the repository root, update releases-chain-v2.txt with the prepared file.
   It preserves the live game/plugin records read during preparation and changes only #HQ.
   If the live feed changes again, replace only its #HQ line using hq-release-record.txt.
   KEEP THE LEADING #. It is an active app-update directive.
5. Commit/upload the repository change yourself. Do not use the SD ZIP's hash in the HQ record.
   Do not reuse revision 40 for a different binary, URL, hash or byte count.

The WUHB is the exact r40 test artifact, not a rebuilt or modified executable.
Native compilation, host regressions, source-routing guards, checksum and ZIP checks passed.
No real Wii U playback/installation confirmation is recorded in this handoff.
The staged release is ready for your upload; no upload, commit or hardware deployment occurred.
No game-content ZIP, plugin update or new game release record is required for this HQ change.
""")
published_receipt = dict(receipt)
published_receipt.update({"status": "Prepared for user-managed release",
                         "preparedAt": datetime.now(timezone.utc).isoformat(),
                         "releaseTag": tag, "feedSource": feed_url,
                         "feedSha256BeforeEdit": hashlib.sha256(live_feed.encode()).hexdigest()})
write("BUILD_RECEIPT.json", json.dumps(published_receipt, indent=2) + "\n")
shutil.copy2(source / "TEST_INSTRUCTIONS.txt", destination / "TEST_INSTRUCTIONS.txt")
files = sorted(path for path in destination.iterdir() if path.is_file())
write("SHA256SUMS.txt", "\n".join(f"{sha(path)}  {path.name}" for path in files) + "\n")
print(destination)
print(record)
