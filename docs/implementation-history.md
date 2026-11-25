# Earlier implementation notes

These development notes describe earlier HQ revisions. Use the root README and current source for r56 behavior.

# Black Ops 2 Unbound HQ

An Aroma/WUHB application that detects the installed Call of Duty: Black Ops II title ID and
system language, locates its registered update on internal or USB storage, updates the Unbound core,
manages local mods, and provides a curated Aroma plugin Workshop.

Current build: **0.0.21-beta / r21**.

## Implemented behavior

- Runs title, storage, local-version, release, and changelog checks at every launch.
- Warns when the detected game's official update is missing, unreadable, or older than v128
  (USA/Europe) or v96 (Japan). The check uses the game SKU rather than the console region and
  adds its actual version to Home without removing storage, region, language, or Unbound details.
  This is a warning, not a new installation/launch block; HQ does not download official updates.
- Reads the backward-compatible `#HQ` record in `releases-chain-v2.txt` before any game-file
  changes. The same downloaded feed is then reused for game-content planning instead of being
  fetched twice. A newer verified WUHB is staged beside the installed app, checked by exact size,
  SHA-256, and WUHB header, then swapped in with the previous WUHB retained as `.unbound-old`.
  HQ returns to the Wii U Menu after success.
- Recognizes USA, three European SKUs, and Japan by their base and update title IDs.
- Uses one universal core download for every supported SKU. Files stored under the archive's
  `@language` component are routed to the selected existing language folder on the console,
  including `english`, `french`, `spanish`, `italian`, `german`, `japanese`, and `fulljap`.
- Uses MCP title information and bounded checks of supported update roots on internal/USB storage.
  Opening/rescanning Install settings or applying a saved manual destination mounts both available
  devices with libmocha to find alternate or duplicate installations. Auto startup does not enumerate
  every alternate root; menu rendering and normal downloads do not repeat this scan.
- Provides a dedicated **Install / Update** menu to choose an existing update destination and one
  of its scanned language folders. Choices are previewed without writing game files, saved in the
  existing `sd:/wiiu/apps/BO2-Unbound-HQ/settings.ini`, and reapplied at future launches. **Reset to
  Auto** restores automatic storage/language selection. An unavailable saved destination or language
  blocks installation until the user selects an available option; HQ does not silently fall back
  to another copy. `content/update.txt` is reread from the selected root before planning an install.
- A language change requires the full release followed by its current patch chain, even when the
  selected root's version marker is current. HQ remembers the pending language installation until
  every package succeeds. The Install menu also offers an explicit full reinstall for the selected
  destination/language when the current version is already installed. Choosing a destination does
  not move the game or change which copy the Wii U Menu launches.
- Uses Aroma's existing writable SD volume at `fs:/vol/external01` without attempting a
  second mount.
- Reports completed startup/detection stages, true download byte progress, and per-file ZIP
  installation progress instead of displaying a decorative loading bar.
- Downloads packages with a bounded receive/write pipeline so network receiving can overlap
  SD writing and SHA-256 verification. Core ZIP installation runs on a worker so progress-screen
  drawing does not pause unpacking. Larger buffered SD writes and reusable ZIP buffers reduce small
  I/O operations; HTTPS, SHA-256, ZIP integrity, free-space checks, and temporary-file writes remain
  enabled. Game-content installs no longer copy originals to SD or automatically roll back after a
  failed install. Low-memory/thread-start failures retain the synchronous fallback paths.
- Retains SHA-identified partial packages after recoverable network interruptions. Retrying the
  same URL/package hash rechecks only the saved prefix, resumes a validated HTTP byte range, and
  verifies the complete SHA-256 before installation. Ignored ranges restart safely from zero.
  Fresh successful downloads use the existing pipeline without prefix rehashing, an extra GET,
  or per-chunk checkpoint writes. Saved partials do not provide power-loss installation recovery.
- Writes bounded, best-effort operation timings to `sd:/wiiu/apps/BO2-Unbound-HQ/performance.log`
  after download/install work finishes; diagnostic logging never determines installation success.
- Opens an on-demand **Support & Stats** screen with **Y** (Wii Remote **2**) from Home. It shows
  application/core/plugin versions, title IDs, official update status, storage/language/paths,
  mounted-storage free space, bounded cache/legacy-backup inventory, Mod Link/module presence, current
  warnings/errors, and recent download/install timing breakdowns. Press **A** to export
  `sd:/wiiu/apps/BO2-Unbound-HQ/diagnostics.txt`; **Y/2** refreshes local information. No network
  requests, repair writes, plugin hashing, or recursive game scans happen during collection, and
  none of these checks run during normal downloads/installation. File presence is not proof a
  plugin is loaded; CPU/FPS/temperature/network link capacity are not measured.
- Opens on the Unbound HQ landing screen that displays the latest server-provided dispatch, falling back
  to the last cached copy and then to a built-in announcement.
- Uses the supplied Black Ops II Unbound boot, GamePad, and logo artwork throughout a
  black/white/cyan/orange graphical storefront.
- Detects the base game on internal memory, USB, or an inserted supported disc and exposes a
  visible Launch Game card. Missing game or update states remain nonfatal browse-only states.
- Enables a plugins-only Workshop. **Unbound Mod Link**, **GamePad Mic Redirect**, and **Protarium**
  are available in r28. Workshop remains usable when Black Ops II is not installed because plugin
  files live on SD. Protarium and GamePad Mic Redirect startup Auto-Updates are optional.
- Opens Workshop immediately from the last saved catalog or its built-in listings, without checking
  every publisher feed first. Press **X** to refresh the remote catalog; opening an available plugin's
  detail page checks that publisher's current release and local install state before offering actions.
- Collects noncritical startup warnings in one controller-scrollable notice after startup, instead of
  interrupting the user with a separate modal for each warning. A required HQ update or a blocking
  recovery condition still receives its own action or warning.
- Keeps the future mods/comments/ratings service out of this release. The reachable plugin pages
  contain real publisher metadata and install state rather than the old local-only demo reviews.
- Provides one local Mod Manager inventory for manifested folders on SD and in the installed update.
  Each logical row shows its SD/console locations, name, author, description, and internal/USB
  storage. Matching names are grouped case-insensitively instead of appearing as unrelated copies.
- Keeps explicit SD-to-console and console-to-SD copy controls for compatibility, but explains that
  Unbound Mod Link is the preferred workflow. Copies are transactional and console-to-SD moves do
  not remove the original until the verified copy finishes and the user confirms deletion.
- Warns when a folder exists in both locations: SD matching files take priority while Mod Link is
  enabled, but console-only files can remain visible and create a mixed version. HQ can remove one
  console duplicate or all duplicates together while preserving every SD copy.
- Detects both historical `sd:/unbound/mods` and `sd:/unbound/mod` roots and offers a safe migration
  to `sd:/unbound/content/mods`. Existing case-insensitive destination collisions, loose files, and
  unsupported entries are never overwritten or silently deleted.
- Provides an **Unbound Mod Link** switch in Mod Manager. The Aroma plugin merges only
  `sd:/unbound/content/mods` over the game's `/vol/content/mods` at runtime, leaving the installed
  game files untouched. The Minus button toggles the shared switch.
- The compact HQ dispatch opens into a dedicated full-text scrolling view.
- Embeds the supplied branding as the Aroma menu icon and TV/GamePad launch screens.
- Uses controller navigation only; touch input is intentionally disabled. The Wii U GamePad,
  Wii U Pro Controller, Classic Controller/Classic Controller Pro, and Wii Remote can all navigate HQ.
- Shows the exact HQ application version and numeric release revision beneath the logo on every
  screen, replacing the obsolete HQ/Workshop header tabs.
- Long status, error, confirmation, and recovery dialogs are clipped to a fixed viewport with a
  visible scrollbar and smooth Up/Down controller scrolling, while their action button stays fixed.
- Reads the installed core version from update `content/update.txt`, compares it with the
  universal GitHub release chain at every launch, and prompts before downloading newer ZIPs.
  Saved installation settings are applied before this comparison so the check uses the selected
  destination's marker rather than an unrelated installation.
- Selects the shortest safe full/patch path: an installed patch level can jump straight to a
  cumulative base-to-latest patch, an older train can jump to a newer full rollup, and a first-time
  install receives the current full package followed by the latest cumulative patch. Each
  successful package is checkpointed before the next begins.
- Downloads over HTTPS, requires SHA-256, and rejects unsafe ZIP paths.
- Embeds curl's Mozilla CA bundle so GitHub HTTPS certificate and hostname verification work
- Waits up to 20 seconds for the Wii U's asynchronous network connection, displays that wait in startup progress, forces IPv4, and retries transient DNS lookup failures
  without an additional Aroma module.
- Caches the last valid release feed and distinguishes a failed update check from an
  up-to-date installation. Network failures display the detailed curl error on screen.
- Core ZIPs can install only to update `code` and update `content`. Former AOC `content/0010`
  payloads are flattened directly into `update/content`; HQ never creates or modifies an AOC
  title or its XML files.
- Plugin packages use an Unbound-owned exact-path allowlist plus a verified release record.
  HQ verifies ZIP size/hash, the exact ZIP entry list, every extracted file's size/hash, and commits
  approved Aroma files with a recovery journal and installed receipt. Existing manual files require an
  explicit **Replace & Manage** confirmation and are never auto-updated silently. GamePad Mic
  Redirect's optional Auto-Update starts OFF; while OFF, startup does not request its publisher feed.
  Unbound Mod Link is managed automatically from the already-downloaded shared release chain, without
  a second startup request. Enabled startup Auto-Updates for these bundled plugins still run
  synchronously before Home appears. Future plugins added only through the online catalog can be
  checked and installed from their detail pages, but cannot enable startup Auto-Update yet.
- Game-content writes are committed through temporary files, with `content/update.txt` written only
  after its package successfully finishes. Originals are not backed up to SD. If installation is
  interrupted, relaunch HQ and retry the same package before playing; files already replaced are not
  automatically restored. HQ's own updater and Aroma plugin updater retain their recovery protection.
  ZIP payloads must not include `update.txt`; HQ owns the installed-version marker.
- Preflights free space on the SD card and the actual MLC/USB destination with an 8 MiB safety
  reserve. Downloads are also capped while streaming so an unknown server size cannot consume the
  remaining reserved space.
- Release downloads use the package size already in their verified metadata instead of making a
  redundant HEAD request. Unknown-size artwork and legacy records retain the size-probe fallback.
  Large game-content files unpack through a bounded 1 MiB queue while a separate worker writes
  their temporary replacements. ZIP input uses aligned read-ahead up to 1 MiB. Small files and
  unavailable worker resources retain direct writes. CRC, exact size, close, recovery and marker-last
  checks remain enabled, with no copied game backups. Exported statistics identify input buffering,
  queued write waits and pipelined file counts; overlapping read/write times must not be added.
  Actual speed depends on the network, SD card and destination device; compare hardware logs.
- Removes only HQ-owned stale cache data: partial downloads, consumed package ZIPs, and cached news
  images no longer referenced by `recent-changes.txt`. Legacy game-content backups, updater recovery
  files, and user mod folders are never included in automatic cleanup. Existing backup folders are
  left untouched, but this build does not create or automatically restore game-content backups.
  Valid saved partial/receipt pairs are retained for retry; unrelated or invalid partial downloads
  are still removed. Changing a release URL/hash cannot append old package bytes to a new release.

This is an Aroma application and requires the Mocha payload/library. Unbound Mod Link uses Aroma's
ContentRedirectionModule, but it is a separate narrowly scoped plugin rather than an SDCafiine pack.

## Choose where Unbound installs

Open **Install / Update** on Home to review the available Black Ops II update installations. The
preview shows game/update storage, region, language, official update version, installed/newest Unbound
versions, update title ID, and the actual `content` path.
The grouped settings and destination summary use a quieter layout with orange focus highlights.
Controls and saved-selection behavior are unchanged from r20.

1. Use **Up/Down** to select **Install destination** or **Content language**.
2. Use **Left/Right** or **A** on either selector to cycle through its choices. **Auto** remains
   available. Language choices come from existing supported language directories in that
   destination's `content` folder, not from a hardcoded region-only choice.
3. Select **Save selection** and press **A**. Browsing choices or saving preferences does not
   download or move game files.
4. Select **Download & install** and press **A** when ready. After a language change, the action
   reads **Install selected language** and downloads the complete compatible release/patch chain.
   If already current, **Reinstall current release** provides the same explicit full-install route.

Press **X** (Wii Remote **1**) to rescan after moving game/update data through Wii U Data Management.
Press **B** to return; HQ asks before discarding unsaved choices. Select **Reset to Auto** to save
automatic storage/language detection again.

Settings stay in the existing `settings.ini`, preserving unrelated plugin preferences. HQ only
offers known, existing Black Ops II update installations on internal memory or USB. This is not an
arbitrary file browser, an SD installation option, or permission to write outside the approved
update `code` and `content` paths. Moving a game between devices remains a Wii U Data Management
operation. When duplicate copies exist, the selected HQ write target and the copy launched by the
Wii U Menu can differ; check the displayed paths before installing.

If the saved device, update root, or language folder is missing or unreadable, HQ shows the problem
and leaves game-content writes blocked instead of installing into another copy. Choose an available
destination/language, save it, or reset to Auto. A language installation interrupted between packages
remains pending across launches until its full chain succeeds, even if one package wrote a current
version marker. Retry before launching the game. This does not add game-content backups or change
the existing download/install pipeline.

## Local SD mods

Place each mod in its own folder under `sd:/unbound/content/mods`. Each folder must contain a
`modload.txt` file. The preferred format is:

```ini
name=Town Survival
description=A custom survival conversion for Town.
author=Example Author
version=1.0
```

HQ also accepts three lines in name/description/author order or one
`name;description;author` line. `version=` and `sync_id=` remain valid optional metadata, but HQ does
not copy or synchronize a second installed copy.

Press Minus in Mod Manager to toggle **Unbound Mod Link**. The visible ON/OFF bubble is status only.
The switch is shared with the plugin's Aroma configuration through
`sd:/unbound/disable_redirect.flag`, so the two interfaces cannot disagree. Restart Black Ops II
after changing the switch. A newly installed or updated WPS requires a full Aroma/console restart.

Use A for the context shown in the footer: copy an SD-only mod to the console, copy a console-only
mod to SD and optionally remove its original, or resolve a duplicated folder by removing its console
copy. HQ warns before installed copies are created because Mod Link normally makes them unnecessary.

Use X on a GamePad/Pro/Classic Controller or 1 on a Wii Remote for the source-specific delete shown
in the footer. On a shared row this removes only the console copy; the SD copy is preserved. Every
deletion uses a temporary recovery name and fails closed if an interrupted transaction needs manual
inspection.

If a power loss leaves a reserved `.unbound-new`, `.unbound-old`, or `.unbound-delete` folder, HQ
fails closed: it leaves the folder untouched and displays recovery guidance. Inspect that folder by
FTP or another trusted file manager before manually restoring or removing it. HQ never treats a
reserved-looking name as proof that the folder belongs to HQ.

## Plugin Workshop and Auto-Update

HQ automatically installs and updates **Unbound Mod Link** from the shared
`releases-chain-v2.txt`. Its ZIP may contain only
`wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps`; HQ validates the archive, exact entry,
file size, and SHA-256 before a transactional replacement. The compatibility filename is retained so
the integrated build replaces the earlier test plugin instead of loading a second competing WPS.

Unbound Mod Link never performs network requests and does not check for or announce downloadable
Unbound content updates. Those checks and all update prompts remain inside HQ, where the user can
choose when to download and install content.

The existing Aroma `ContentRedirectionModule.wms` is required for runtime linking.
`NotificationModule.wms` is used only for the brief Mod Link active toast; linking still works
without notifications. HQ's Mod Link package contains only the Unbound WPS and never bundles or
replaces either shared module.

**GamePad Mic Redirect** is an experimental publisher-owned plugin that redirects the Wii U
GamePad's built-in microphone for games that normally accept headset voice input. Its v0.4.0
release includes mute and push-to-talk controls. HQ installs only the approved
`wiiu/environments/aroma/plugins/GamePadMicRedirect.wps` path from its developer-owned feed at
`tonytrawl/gamepad_mic_redirect`; it does not grant that feed authority over any other SD path.

Open **Workshop**, select GamePad Mic Redirect, then choose **Install**. If an approved
plugin file already exists without an HQ receipt, the action changes to **Replace & Manage** and
requires a stronger confirmation. No Workshop package writes to BO2's game/update paths.

**Protarium** connects compatible Wii U games to Protarium Network. HQ r28 installs its paired
Inkay Aroma module and plugin from the reviewed publisher feed:

```text
https://raw.githubusercontent.com/Protarium-Network/Inkay-GitHub-Release/main/unbound-plugin-release.txt
https://github.com/Protarium-Network/Inkay-GitHub-Release
```

Its package may contain only `wiiu/environments/aroma/modules/Inkay-pretendo.wms` and
`wiiu/environments/aroma/plugins/Inkay-pretendo.wps`. Open **Workshop > Protarium**, then choose
**Install**, or explicitly confirm **Replace & Manage** for an existing manual installation.
Both files are verified and staged before replacement, with a recovery journal and restoration
attempted if installation fails. Follow any recovery instructions before retrying; do not delete
pending files. Fully restart the console/Aroma environment afterward. Inkay changes networking
and SSL behavior while active; read the publisher's safety notes about reduced SSL verification.

After HQ has successfully installed or adopted the current release, the second plugin-page action can
turn **AUTO-UPDATE ON**. Protarium and GamePad Mic Redirect settings default to OFF when no
preference is saved; existing saved ON preferences are preserved. The settings are stored as:

```ini
plugin_auto_update.gamepad_mic_redirect=on
plugin_auto_update.protarium=on
```

When enabled, startup checks the developer-owned feed only after the HQ application update check. A
newer monotonic plugin revision is installed without another prompt only when the current files are
healthy and HQ-managed. Manual/unmanaged, damaged, downgraded, same-revision-mutated, malformed, or
offline states are reported and left untouched. Fully restart the Wii U/Aroma environment after an
install or update; already loaded `.wms` and `.wps` code cannot be replaced in memory.

When either external plugin's Auto-Update setting is OFF, startup makes no Workshop-catalog or
publisher-release request for that plugin. Workshop first displays the saved catalog, falling back to the
built-in listings. Press **X** there to refresh the team-controlled catalog from GitHub. Open an
available plugin's detail page to refresh its publisher feed and inspect the installed files; only
then does HQ offer an install or update based on verified current metadata. An offline refresh does
not erase the saved listing, but a cached card is not proof that its release is current. Protarium
and GamePad Mic Redirect with Auto-Update ON receive synchronous verified startup checks; Unbound Mod Link
continues its automatic startup check using the release-chain response HQ already needs, adding no
extra HTTP request. Future catalog-only plugins show **Auto-Update unavailable** until HQ supports
startup checks for them. The Mod Link runtime plugin performs no network work in games or on the Wii U
Menu.

## Configure the server

The beta endpoints in `include/config.hpp` target `tonytrawl/bo2-unbound`. Publish the patch-aware
feed as `releases-chain-v2.txt` on
the repository's `main` branch and put ZIP packages in versioned GitHub Releases as described
in `server-example/README.md`. Publish `workshop-plugins.txt` at the repository root for the curated
plugin list. Publish assets before activating any release manifest.

Protarium integration is enabled in r28: include its reviewed record from
`server-example/github-root/workshop-plugins.txt` in the published catalog. The publisher's separate
repository and paired-file feed contract are documented in `server-example/PROTARIUM_PLUGIN_PUBLISHING.md`.
Publish the tested HQ asset before activating its `#HQ` release record; keep existing game-content
and Unbound Mod Link records unchanged.

GamePad Mic Redirect owns its release metadata at
`https://raw.githubusercontent.com/tonytrawl/gamepad_mic_redirect/main/release.txt`, with project
information at `https://github.com/tonytrawl/gamepad_mic_redirect`. Its external feed may update the
versioned package metadata and bytes, while the Unbound-owned catalog permanently limits installation
to `wiiu/environments/aroma/plugins/GamePadMicRedirect.wps`.

HQ application releases use their own `hq-v<VERSION>` GitHub tags, but their activation record is
stored in the shared `releases-chain-v2.txt` as `#HQ|REVISION|VERSION|URL|SHA256|FILE_BYTES`.
The leading `#` makes old HQ builds treat the line as an ordinary comment, while self-updating builds
read it before parsing the normal `UNIVERSAL|FULL` and `UNIVERSAL|PATCH` records. Every WUHB has a
monotonically increasing numeric revision compiled into the app. If a newer revision is advertised,
declining it or an installation failure puts that run into read-only mode: news and game launch
remain available, but core, Workshop, and Mod Manager writes are blocked. HQ also compares the live
record with its last valid cached record, so an outage or accidental feed rollback cannot hide a
newer revision it has already seen. A malformed `#HQ|` directive fails closed; when no newer cached
revision is known, an ordinary network outage warns the user without stranding offline users.

Revision 6 and later explicitly unmount Aroma's currently running WUHB bundle before the verified
same-directory swap, matching the official Aroma Updater mechanism. The private revision-4/5 test
builds cannot unlock their own running WUHB and require one final manual SD-card replacement on test
consoles. Those updater builds were never distributed publicly; revision 6 is the first public
self-updating baseline, and automatic HQ updates work from revision 6 onward.

Revision 10 requires the static `workshop-plugins.txt` catalog for its active plugins-only Workshop.
A verified Unbound Mod Link package is declared in the same release chain with:

```text
#PLUGIN|UNBOUND_MOD_LINK|REVISION|VERSION|ZIP_URL|ZIP_SHA256|ZIP_BYTES
#PLUGIN_FILE|UNBOUND_MOD_LINK|wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps|WPS_SHA256|WPS_BYTES
```

A plugin revision is monotonic package metadata; it is independent of the Black Ops II Unbound
content version. Publish both immutable assets first, verify their public URLs, and update the shared
feed last.

A future user-mod catalog, comments, ratings, and uploads should use the versioned JSON catalog and
moderated API proposed in `server-example/SERVER_ARCHITECTURE.md`; those services are not active yet.

### Rich HQ updates

`recent-changes.txt` supports a deliberately small Markdown-style format:

```md
# Large heading
## Medium heading
### Small orange heading

Normal text with **bold words** and *italic words*.

- First list item
- Second list item

---

![Image caption](https://raw.githubusercontent.com/owner/repository/main/news/example.png)
```

Image directives should be on their own line and must use HTTPS. HQ caches each image by URL on
the SD card, limits each download to 8 MiB, preserves its aspect ratio, and shows an unavailable
placeholder without losing the surrounding text if it cannot be downloaded or decoded. For clear
GamePad rendering, use PNG or JPEG images around 1080 pixels wide and avoid text baked into images.

## Package builder

`tools/unbound_packager/dist/Unbound Package Builder.exe` is a standalone Windows GUI for
creating universal full ZIPs and incremental patches. It compares complete version snapshots,
includes only changed/new files in patches, rejects unsafe deletion patches, calculates package/file SHA-256
values, previews the exact server record, and atomically updates an explicitly selected
`releases-chain-v2.txt` while preserving `#HQ`, comments, blank lines, and unrelated packages.
Its `release-catalog.json` is local cumulative-patch history for the builder; keep it with your
package output, but do not upload or commit it.

See `tools/unbound_packager/README.md` before publishing. Patch versions use the base-attached
form `0.0.05P1.0`. In the patch-aware feed, `FROM_VERSION` is the oldest compatible checkpoint,
not an exact required predecessor. The updater prefers cumulative shortcuts and full rollups so
users do not download every historical patch.

## Build

With devkitPro installed:

```sh
dkp-pacman -Syu --needed wiiu-dev wiiu-portlibs
make
```

Install libmocha into wut first if it is not already present. The output is
`Unbound-HQ.wuhb`. The first startup from the old BO2 path moves the app and its settings to `sd:/wiiu/apps/Unbound-HQ/` and asks you to restart the console.

Or use Docker:

```sh
docker build -t bo2-unbound-hq-builder .
docker run --rm -v "$PWD:/project" bo2-unbound-hq-builder make
```

## Controls

- Supported controllers: Wii U GamePad, Wii U Pro Controller, Classic Controller/Classic Controller Pro,
  and Wii Remote. Controllers exposed to the console as a Wii U Pro Controller (for example through
  Bloopair) use the same Pro Controller mapping.
- D-Pad: move between cards, buttons, and scrollable content
- Left stick (GamePad, Pro Controller, or Classic Controller): menu navigation and scrolling
- A: open or activate the selected visible control
- Install / Update: Up/Down selects a row; Left/Right or A cycles destination/language; A on
  **Save selection**, **Download & install**, **Reinstall current release**, or **Reset to Auto**
  performs that named action. X / Wii Remote 1 rescans locations; B returns.
- X: refresh the remote catalog in Workshop; delete the selected SD mod folder in Mod Manager
- B: return to the previous screen or cancel
- Minus (inside Mod Manager): toggle Unbound Mod Link; the ON/OFF bubble is informational
- Y / Wii Remote 2: open Support & Stats from Home, or refresh the open report
- A (in Support & Stats): export the report to the HQ folder on SD
- Plus: exit

Plus performs a ProcUI handoff back to the Wii U Menu after closing network and mounted storage.

Touch input is disabled. Use Up/Down to scroll and Left/Right to move through horizontal items
or persistent actions. When a dialog displays a scrollbar, Up/Down scrolls its complete message
without moving the persistent A/B action.

## Safety boundary

The application modifies installed game files. Test every supported title ID and language route
on real hardware before publishing a universal release. A failed game-content install can leave a
mixture of old and new files until the same package is successfully retried; the version marker is
not advanced on failure. Keep a known-good way to reinstall the official game update and Unbound.
Legacy backups are not deleted, and HQ/plugin updater recovery remains enabled. Only publish files
you have the rights to distribute.
