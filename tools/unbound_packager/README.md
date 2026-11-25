# Unbound Package Builder

This Windows app turns a complete Black Ops 2 Unbound file snapshot into a verified universal ZIP,
creates its manifest, and updates the shared `releases-chain-v2.txt` used by HQ. It supports full
releases and cumulative add/replace patches. It never commits, tags, pushes, or uploads anything.

## Open the builder

Run:

```text
dist/Unbound Package Builder.exe
```

No Python installation is required for the EXE. `Launch Unbound Package Builder.bat` opens the EXE
when present and otherwise tries the Python source version.

## The three-step workflow

### 1. Choose the release

- **Full release** packages every staged file. Use it for a new base, after removing shipped files,
  or when you intentionally want one complete download.
- **Patch / cumulative update** compares two complete snapshots and packages new or changed files.
- Enter the target version, such as `0.0.05` or `0.0.05P1.0`.
- The standard GitHub tag is always `v<VERSION>`—for example, `v0.0.05P1.0`.
- For a patch, **Oldest supported version** is the oldest install that may safely receive the ZIP.

### 2. Choose files and publishing locations

The current snapshot must be the complete final state for the target version:

```text
my-snapshot/
  update/
    code/
    content/
      @language/
```

Use **Create empty snapshot layout** if needed. Put former DLC/AOC files directly in
`update/content`; HQ now installs all served game content into the registered update title. Put
localized files below `@language`; HQ replaces that component with the console's game language.
Never stage `update/content/update.txt`, because HQ owns the installed-version marker.

The validator rejects common mistakes, including an extra outer directory, `update/meta`, literal
language directories, symlinks, duplicate case-insensitive ZIP paths, and unexpected top-level
folders. A legacy `aoc/content/0010` tree is accepted only as a migration aid and is remapped into
`update/content`.

Choose two publishing locations:

1. **Package output folder** — receives ZIPs, manifests, and the internal patch-history catalog.
   Reuse this folder between patch builds.
2. **Shared release feed** — explicitly select the repository's `releases-chain-v2.txt`. The builder
   atomically inserts or replaces only the matching package record. Existing `#HQ`, `#PLUGIN`, and
   `#PLUGIN_FILE` metadata, comments, blank lines, and unrelated package records are preserved.

### 3. Preview, then build

**Validate & Preview** hashes both snapshots and shows:

- the exact ZIP and manifest names;
- the GitHub asset URL/feed-record format;
- every path that will be included;
- the selected shared feed path.

The SHA-256 and compressed-byte fields are filled after the deterministic ZIP is built. Then choose
**Build & Update Feed**. The finished review panel shows the exact seven-field feed record and the
next publishing steps.

If the ZIP succeeds but the selected feed cannot be updated, the builder reports a prominent
warning instead of claiming the whole build failed. The ZIP and manifest remain valid, and the exact
record is available through **Copy feed record** for manual recovery.

## Patch behavior

Patches are add/replace-only because the current Wii U updater does not provide transactional file
deletion. If a staged file disappeared between snapshots, the builder refuses the patch and tells
you to publish a full release.

For a patch, select:

- **Oldest-version snapshot**: the complete installed state matching `FROM_VERSION`;
- **Current complete snapshot**: the complete final state at `TO_VERSION`.

The output name includes both endpoints:

```text
unbound-universal-0.0.05-to-0.0.05P1.0-patch.zip
```

`release-catalog.json` is retained beside the packages solely as local cumulative-patch history.
The builder uses its `filePaths` records to include the final copy of anything changed by an
intermediate patch—even if that file later reverted to the base bytes. This prevents someone on an
intermediate patch from being left stale. Keep this file with the builder output, but there is no
reason to hand-edit it or make it the center of the publishing workflow.

If relevant old catalog data lacks `filePaths`, includes deletions, or references a file missing
from the target snapshot, the builder refuses to guess. Build a new full release instead.

## Outputs and publishing

A full build produces files like:

```text
unbound-universal-0.0.06-full.zip
unbound-universal-0.0.06-full.manifest.json
release-catalog.json
```

It also updates the explicitly selected `releases-chain-v2.txt` wherever that file lives.

After building:

1. Create or open the GitHub Release whose tag exactly matches `v<VERSION>`.
2. Upload the ZIP and its `.manifest.json` sidecar.
3. Verify that the release asset URL downloads successfully.
4. Review the local feed change.
5. Commit and push the feed yourself when ready.
6. Keep `release-catalog.json` in the output workspace for future cumulative builds; it does not
   need to be uploaded or committed.

The builder performs none of steps 1, 2, or 5 automatically.

## Safety limits

- Universal update-only destination roots are `update/code` and `update/content`.
- Snapshot symlinks and case-colliding paths are rejected.
- Packages are limited to 4,096 files and 5 GiB total uncompressed (requires HQ r30 or newer).
- Individual payload files remain limited to 2 GiB. Each downloadable ZIP must remain smaller
  than 2 GiB; the larger expanded limit does not raise the download/per-file limits.
- ZIPs and text/JSON metadata use temporary files and atomic replacement.
- Every package and every payload file receives a SHA-256 digest.
- Already-compressed game assets are stored without wasting time recompressing them.

## Run tests or rebuild from source

From this directory with Python 3 and Tkinter:

```powershell
python -m unittest discover -s tests -v
pyinstaller "Unbound Package Builder.spec" --noconfirm --clean
```
