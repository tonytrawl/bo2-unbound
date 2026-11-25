"""Stage the tested r42 binary and a publisher handoff; never publish or commit."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request
import zipfile

project = Path(__file__).resolve().parents[1]
source = project / "staging-r42/BO2-Unbound-HQ-v0.0.42-beta-TEST"
destination = project / "release-r42"
receipt = json.loads((source / "BUILD_RECEIPT.json").read_text(encoding="utf-8"))

def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

assert receipt["revision"] == 42 and receipt["version"] == "0.0.42-beta"
assert sha(source / "BO2-Unbound-HQ.wuhb") == receipt["wuhbSha256"]
assert (source / "BO2-Unbound-HQ.wuhb").stat().st_size == receipt["wuhbBytes"]
for name, expected in receipt["sourceSha256"].items():
    assert sha(project / name) == expected, name

feed_url = "https://raw.githubusercontent.com/tonytrawl/bo2-unbound/main/releases-chain-v2.txt"
with urllib.request.urlopen(feed_url, timeout=25) as response:
    live_feed = response.read().decode("utf-8-sig")
assert live_feed.startswith("# UNBOUND_RELEASE_CHAIN_V2")
live_hq = [line for line in live_feed.splitlines() if line.startswith("#HQ|")]
assert len(live_hq) == 1 and int(live_hq[0].split("|")[1]) < 42, live_hq
tag = "hq-v0.0.42-beta"
record = (f"#HQ|42|0.0.42-beta|https://github.com/tonytrawl/bo2-unbound/releases/download/{tag}/"
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
archive = destination / "BO2-Unbound-HQ-v0.0.42-beta-sd.zip"
shutil.copy2(source / "BO2-Unbound-HQ-v0.0.42-beta-TEST-sd.zip", archive)
with zipfile.ZipFile(archive) as packed:
    assert packed.namelist() == ["wiiu/apps/BO2-Unbound-HQ/BO2-Unbound-HQ.wuhb"]
    assert packed.testzip() is None
    assert hashlib.sha256(packed.read(packed.namelist()[0])).hexdigest() == receipt["wuhbSha256"]
write("hq-release-record.txt", record + "\n")
write("releases-chain-v2.txt", staged_feed)
write("RELEASE_NOTES.txt", """## Unbound HQ 0.0.42-beta

- Fixes installation for players using a game disc. HQ now reads the original game files from the disc your Wii U has already mounted.
- Repairs consoles where an earlier HQ test build replaced the game's shared movie folder. HQ puts the original folder back, keeps Unbound movies installed and frees the copied movies.
- Support & Stats export now also saves a read-only folder report to help us make future installs smaller.
- Keeps installation recovery, progress reporting and full download verification.

Update HQ, relaunch, then open Install / Update. Disc players: keep the Black Ops II disc inserted while installing.
If HQ offers to put the original movie folder back, let it finish before playing.
For manual installation, extract the SD ZIP onto your SD card, keeping your settings and cache.
""")
write("DISCORD_ANNOUNCEMENT.txt", """## 💿 HQ 0.0.42 IS OUT: DISC PLAYERS, YOU'RE IN!

If you play from a disc, HQ was stopping with "could not read the original files from the matching game disc". That's fixed. HQ now reads straight from the disc your Wii U already has mounted.

**Open HQ, accept the update, relaunch, then open Install / Update.** Keep your Black Ops II disc inserted while it installs.

Your saved download is checked first. **If it's complete and valid, you won't need to download it again.**

Thanks to everyone who sent reports. Enjoy Beta 3! 🔥
""")
write("UPLOAD.txt", f"""BLACK OPS 2 UNBOUND HQ 0.0.42-beta / r42

1. Create a GitHub release in tonytrawl/bo2-unbound.
   Tag: {tag}
   Title: Unbound HQ 0.0.42-beta
2. Attach BO2-Unbound-HQ.wuhb with that exact filename. This is the self-updater's asset.
   Optionally attach BO2-Unbound-HQ-v0.0.42-beta-sd.zip for manual SD installation.
   Use RELEASE_NOTES.txt as the release description.
3. Publish the release and confirm its WUHB asset downloads before changing the feed.
4. At the repository root, update releases-chain-v2.txt with the prepared file.
   It preserves the live game/plugin records read during preparation and changes only #HQ.
   If the live feed changes again, replace only its #HQ line using hq-release-record.txt.
   KEEP THE LEADING #. It is an active app-update directive.
5. Commit/upload the repository change yourself. Do not use the SD ZIP's hash in the HQ record.
   Do not reuse revision 42 for a different binary, URL, hash or byte count.

The WUHB is the exact r42 test artifact Tony ran, not a rebuilt or modified executable.
Tony reported on October 27, 2026 that the r42 test build worked on hardware.
Native compilation, host regressions, source-policy guards, checksum and ZIP checks passed.
No upload, commit or deployment occurred while preparing this folder.
No game-content ZIP, plugin update or new game release record is required for this HQ change.
""")
published_receipt = dict(receipt)
published_receipt.update({"status": "Prepared for user-managed release",
                         "preparedAt": datetime.now(timezone.utc).isoformat(),
                         "hardwareResult": "Tony reported the r42 test build worked (2026-10-27)",
                         "releaseTag": tag, "feedSource": feed_url,
                         "feedHqBeforeEdit": live_hq[0],
                         "feedSha256BeforeEdit": hashlib.sha256(live_feed.encode()).hexdigest()})
write("BUILD_RECEIPT.json", json.dumps(published_receipt, indent=2) + "\n")
shutil.copy2(source / "TEST_INSTRUCTIONS.txt", destination / "TEST_INSTRUCTIONS.txt")
files = sorted(path for path in destination.iterdir() if path.is_file())
write("SHA256SUMS.txt", "\n".join(f"{sha(path)}  {path.name}" for path in files) + "\n")
print(destination)
print("live:", live_hq[0])
print(record)
