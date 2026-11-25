# Black Ops 2 Unbound HQ

This repository hosts the public manifests and release metadata used by the Black Ops 2
Unbound HQ Wii U application. Large packages are attached to versioned GitHub Releases and
are not committed to Git history.

## Current beta

The fixed HQ build uses one universal update-only payload shared by USA, European, German,
and Japanese title IDs:

- `unbound-universal-0.0.05-full.zip`

The HQ app detects the installed title ID and system language, maps `@language` FF paths to the
correct localized content directory, verifies the SHA-256 hash, installs everything into the
registered update title, and writes `update/content/update.txt` only after the archive succeeds.

The curated plugins-only Workshop offers Unbound Mod Link, the experimental GamePad Mic Redirect
plugin, and Protarium Network. HQ revision 28 enables Protarium installation and optional
startup Auto-Update. The published `workshop-plugins.txt` includes its reviewed record. HQ
manages Mod Link through the shared release chain and follows only available external plugins'
developer-owned feeds within the exact Aroma paths approved by this repository. Future
user-uploaded mods/comments remain disabled.

Workshop opens immediately with its last saved catalog or built-in listings. Press **X** to refresh
this repository's `workshop-plugins.txt`; opening an available plugin's detail page checks the
publisher release feed and installed files before offering install/update controls. A saved listing
is useful offline, but does not claim that its release is current. Unbound Mod Link's automatic check
and enabled Protarium/GamePad Mic Redirect Auto-Updates perform their verified startup checks. A future
plugin added only through the online catalog can be checked from its detail page, but startup
Auto-Update is unavailable for it.
Noncritical startup warnings appear together in one scrollable notice rather than separate popups.

## Publishing order

1. Create a prerelease with a version newer than the last split/AOC payload.
2. Upload the full update-only ZIP and its manifest.
3. Verify the uploaded asset name and SHA-256 match the generated feed record.
4. Copy full and patch records into `releases-chain-v2.txt`, then commit and push that activation last.
   Keep `releases-update-only.txt` pinned to the newest full package for older HQ builds.

Publishing the manifest last prevents the Wii U application from discovering a release whose
assets are not online yet. Never replace a published asset in place; publish a new version with
new hashes.

HQ application and Unbound Mod Link updates use their own GitHub tags, but share
`releases-chain-v2.txt` with the Black Ops II content graph. Publish and verify each asset first,
then add a
`#HQ|REVISION|VERSION|URL|SHA256|FILE_BYTES` record with a strictly increasing numeric revision.
The leading `#` keeps the record invisible to older content-only HQ builds.

For patch records, `FROM_VERSION` is the oldest compatible checkpoint rather than an exact
predecessor. Because packages are add/replace-only, a cumulative `0.0.05 -> 0.0.05P50.0` ZIP can
update consoles on `0.0.05`, `P1`, through `P49` in one download. Periodic newer full snapshots
can also update any older installation directly. The app chooses the fewest packages first, then
the smallest total download among paths with the same package count.

A cumulative ZIP must carry the final target copy of every file touched by any intervening patch,
including a file later reverted to its base contents. Preserve package manifests so this union can
be audited; a base-versus-latest diff alone does not contain enough history to prove that guarantee.

## Repository files

- `releases-chain-v2.txt` — shared HQ-app record, Mod Link package metadata, and patch-aware
  game-content graph.
- `releases-update-only.txt` — frozen full-only feed for older HQ builds.
- `releases.txt` — frozen legacy feed for older HQ builds; do not advertise new packages here.
- `workshop-plugins.txt` — active curated Aroma plugin list and exact write-path allowlists.
- `workshop.txt` — frozen legacy mod listing; revision 11 does not read it.
- `recent-changes.txt` — the HQ landing-page announcement.
- `about.txt` — project description for the future About screen.
- `manifests/` — auditable per-file release manifests and checksums.

Comments, votes, and user uploads will require a separate authenticated/moderated service.
GitHub remains the source for team-controlled static manifests and release downloads.

## Unbound Mod Link publishing

Publish `BO2-Unbound-Mod-Link-v<VERSION>.zip` as an immutable release asset. It must contain exactly:

```text
wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps
```

After its public URL, archive SHA-256/bytes, and WPS SHA-256/bytes are verified, update the shared
release chain last:

```text
#PLUGIN|UNBOUND_MOD_LINK|REVISION|VERSION|DOWNLOAD_URL|ARCHIVE_SHA256|ARCHIVE_BYTES
#PLUGIN_FILE|UNBOUND_MOD_LINK|wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps|FILE_SHA256|FILE_BYTES
```

HQ installs and updates this plugin automatically. The plugin does no HTTP work and does not show
downloadable-content update notifications. HQ owns the normal `UNIVERSAL` version comparison and
all user-facing update prompts.

## GamePad Mic Redirect publishing split

This repository owns GamePad Mic Redirect's curated listing and exact destination allowlist in
`workshop-plugins.txt`. Its developer-owned metadata and project information live at:

```text
https://raw.githubusercontent.com/Unbound-Project/gamepad_mic_redirect/main/release.txt
https://github.com/Unbound-Project/gamepad_mic_redirect
```

Its immutable release archive may contain only:

```text
wiiu/environments/aroma/plugins/GamePadMicRedirect.wps
```

The plugin redirects the Wii U GamePad's built-in microphone for headset-only voice input. It is
experimental; v0.4.0 includes mute and push-to-talk controls. Publish and verify its GitHub Release
asset first, then increase the external feed's monotonic revision and update that feed last. Never
reuse a revision or replace a released asset in place. A full console or Aroma-environment restart
is required after HQ installs or updates the WPS.

## Protarium Network

HQ revision 28 enables **Workshop > Protarium** for installation and optional startup Auto-Update.
This repository owns the curated listing and exact destination allowlist. Protarium's publisher
owns the version, immutable package URL, ZIP hash/size, and per-file hash/size records at:

```text
https://raw.githubusercontent.com/Protarium-Network/Inkay-GitHub-Release/main/unbound-plugin-release.txt
https://github.com/Protarium-Network/Inkay-GitHub-Release
```

The external feed must point at a complete immutable `wiiu.zip` containing exactly:

```text
wiiu/environments/aroma/modules/Inkay-pretendo.wms
wiiu/environments/aroma/plugins/Inkay-pretendo.wps
```

Publish and verify the GitHub Release asset first, then increase the feed's monotonic revision and
update that feed last. Never reuse a revision or replace a released asset in place. HQ verifies the
ZIP and both files before replacing the pair, with a recovery journal and restoration attempted
if the update fails. Follow any recovery instructions before retrying; do not delete pending files.
A full console or Aroma-environment restart is required after HQ installs either file.

Existing manual installations require explicit **Replace & Manage** confirmation before HQ can
replace or auto-update them. Protarium/Inkay changes networking and SSL behavior while active;
read the publisher's safety notes about reduced SSL verification before enabling it.

Protarium and GamePad Mic Redirect Auto-Updates are opt-in and default to OFF when no preference
is saved. Existing saved ON preferences are retained. When OFF, HQ does not request that plugin's
publisher feed at startup. Unbound Mod Link remains automatically checked through the shared release chain. Opening
Workshop shows saved or built-in cards immediately; the user presses **X** to refresh the central
catalog, while opening a plugin detail page checks its publisher feed and install state. A future
catalog-only plugin shows **Auto-Update unavailable** rather than offering a nonfunctional startup
switch.
