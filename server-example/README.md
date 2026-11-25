# GitHub distribution setup

Use a public GitHub repository for content controlled by the Unbound team: the universal core
packages, release manifest, Workshop manifest, What's New text, and About text. Keep large ZIPs
out of Git history and attach them to versioned GitHub Releases.

Recommended repository layout:

```text
bo2-unbound/
  releases-chain-v2.txt
  releases-update-only.txt
  workshop-plugins.txt
  recent-changes.txt
  about.txt
  README.md
```

Patch-aware HQ reads `releases-chain-v2.txt`; older update-only HQ builds remain on the frozen
full-only `releases-update-only.txt`. `about.txt` is ready for a future About
screen but is not consumed by this build yet. All files must be UTF-8.

## 1. Create and connect the repository

1. Create a public GitHub repository named `bo2-unbound`.
2. Copy the supplied root text files from this directory into its default `main` branch.
3. Replace `YOUR_GITHUB_NAME` in `include/config.hpp` with the repository owner and rebuild the
   WUHB. If you choose another repository name, change that part of all three URLs too.
4. Do not commit private keys, GitHub tokens, moderation credentials, or unpublished packages.

The resulting manifest URLs are:

```text
https://raw.githubusercontent.com/OWNER/bo2-unbound/main/releases-update-only.txt
https://raw.githubusercontent.com/OWNER/bo2-unbound/main/releases-chain-v2.txt
https://raw.githubusercontent.com/OWNER/bo2-unbound/main/recent-changes.txt
https://raw.githubusercontent.com/OWNER/bo2-unbound/main/workshop-plugins.txt
https://raw.githubusercontent.com/OWNER/bo2-unbound/main/about.txt
```

## 2. Publish an HQ application update

HQ application assets and Black Ops II content packages use separate GitHub Release tags, but both
are activated through `releases-chain-v2.txt`. Build and test `BO2-Unbound-HQ.wuhb`, then publish it
as an asset on a tag such as `hq-v0.0.15-beta`. Compute its exact byte size and SHA-256 and add the
special comment record only after the asset URL works:

```text
# HQ fields: REVISION|VERSION|DOWNLOAD_URL|SHA256|FILE_BYTES
#HQ|15|0.0.15-beta|https://github.com/OWNER/bo2-unbound/releases/download/hq-v0.0.15-beta/BO2-Unbound-HQ.wuhb|64_HEX_SHA256|EXACT_BYTES
```

`REVISION` is the authoritative comparison value and must be a positive, monotonically increasing
integer. Increment both the feed revision and `config::AppReleaseRevision` for every published
WUHB, including beta rebuilds and hotfixes. `VERSION` is its user-facing label. Never replace an
asset or reuse a revision: create a new tag, filename/hash record, and higher revision.

The `#HQ` prefix is intentional. Older HQ builds ignore it as a normal comment, preserving their
ability to read the content records below it. Self-updating builds recognize it, check the app
revision first, and then reuse the already-downloaded feed for game-content planning.

On startup HQ checks the shared feed before detecting or updating game content. A newer WUHB is
downloaded to the same SD directory, streamed through SHA-256 verification, checked against the
declared exact size and `WUHB` header, and then committed. A previously verified staged WUHB is
re-hashed and reused after a retry instead of being downloaded again. The current canonical WUHB
becomes `BO2-Unbound-HQ.wuhb.unbound-old`; HQ attempts to restore it if the final rename fails. If
that restore also fails, the known-good file remains at `.unbound-old.pending` for PC recovery.
After a successful update HQ safely returns to the Wii U Menu. It does not attempt an unsafe
in-process restart.

Revision 6 and later use `RPXLoader_UnmountCurrentRunningBundle()` before the WUHB rename transaction.
This is required because Aroma mounts the running WUHB as `/vol/content`. The private revisions 4 and 5
used during updater development lack that unmount step and require a one-time manual replacement on any
test console. They were never distributed publicly, so revision 6 is the first public self-updating
baseline.

The canonical install path must be:

```text
sd:/wiiu/apps/BO2-Unbound-HQ/BO2-Unbound-HQ.wuhb
```

If the shared feed advertises a newer revision and the user declines it or installation fails, HQ blocks
all game-file writes for that run while leaving news and game launch usable. HQ retains the highest valid
application revision from its cached copy of this same feed, so an outage or accidental server rollback
cannot bypass a newer update it has already seen. A malformed `#HQ|` directive fails closed. If the feed
is merely offline and no newer cached revision is known, HQ warns but continues so a GitHub outage does
not strand users. Noncritical startup warnings are collected into one scrollable notice; a required
HQ update or recovery condition still gets its dedicated handling.

## 3. Package an Unbound core update

Create one update-only ZIP shared by every supported SKU. ZIP is the intended format: the
application uses libzip and extracts each entry in chunks, so it does not hold the whole archive
in Wii U memory. Use this exact layout:

```text
unbound-universal-1.0.0-full.zip
  update/
    code/
      ...files written to the BO2 update code folder...
    content/
      ...shared files written to the BO2 update content folder...
      @language/
        ...localized FF files...
      ...former AOC content/0010 files served directly through the update title...
```

Do not include an `aoc/` archive root or any DLC XML. HQ installs only through the already
registered BO2 update title and never creates or modifies an AOC title.

Do not put the title-ID directory structure in the ZIP. HQ maps each supported update title ID
to the correct installed location. `@language` must be an entire path
component. During extraction HQ replaces it with the selected folder: `english`, `french`,
`spanish`, `italian`, `german`, or `japanese`.

Do not include `update.txt`; after every file succeeds, the application writes the installed version to:

```text
<BO2 update title>/content/update.txt
```

Only `update/code/...` and `update/content/...` are accepted. Absolute paths, `..`, symlinks,
and any other root are rejected. Keep each package below the app's current 2 GiB
uncompressed safety limit and 4,096 entries.

### Title and language routing

| Package key | Base / update suffix | Language selection |
|---|---|---|
| USA | `1010CF00` | French → `french`; Spanish → `spanish`; otherwise `english` |
| EUR_EN_FR | `10113400` | French → `french`; otherwise `english` |
| EUR_ES_IT | `10113500` | Italian → `italian`; otherwise `spanish` |
| EUR_DE | `10113700` | Always `german` |
| JPN | `1011B400` | Always `japanese` |

The package bytes are identical across these rows; only the destination title roots and
localized path component change.

## 4. Publish the ZIP as a GitHub Release

1. Compute every ZIP's SHA-256 before upload:

   ```powershell
   (Get-FileHash .\unbound-universal-1.0.0-full.zip -Algorithm SHA256).Hash.ToLowerInvariant()
   ```

2. On GitHub, open **Releases**, create tag `v1.0.0`, and attach the universal ZIP.
3. Publish the release, then use its immutable versioned asset URL in
   `releases-update-only.txt` and as a `FULL` record in `releases-chain-v2.txt`:

   ```text
   UNIVERSAL|1.0.0|UPDATE_URL|UPDATE_SHA256
   UNIVERSAL|FULL|-|1.0.0|UPDATE_URL|UPDATE_SHA256|COMPRESSED_BYTES
   ```

4. Add the universal records, verify the URL downloads, then commit the feeds last.
   This prevents clients from seeing a manifest before its packages exist.
5. Never replace a published asset in place. Publish a new version/tag, new filename, new hash,
   and then update the manifest.

For a patch, use a base-attached patch version such as `0.0.05P1.0` and tag
`v0.0.05P1.0`. Add a `PATCH` edge to `releases-chain-v2.txt`:

```text
UNIVERSAL|PATCH|0.0.05|0.0.05P1.0|PATCH_URL|PATCH_SHA256|COMPRESSED_BYTES
```

Keep the full `0.0.05` edge in that file. `FROM_VERSION` is the minimum compatible checkpoint,
not an exact required predecessor: a cumulative `0.0.05 -> 0.0.05P50.0` patch can be applied by
users on `0.0.05`, `P1`, through `P49`. A new installation downloads the full package and then
that cumulative patch. Add cumulative shortcuts as the patch level grows, and periodically
publish a newer full rollup; an older installed version may jump directly to that complete full
snapshot. HQ prefers the fewest packages and then the smallest total download.

These jumps are safe only because HQ core ZIPs are add/replace-only **and** a cumulative package
contains the final target copy of every path touched by any patch after its `FROM_VERSION`. A
simple base-versus-latest diff is insufficient if an intermediate patch changed a file that was
later reverted to its base contents. Keep all intermediate package manifests and include that
file's latest copy in the cumulative ZIP. If a release ever needs to remove a previously shipped
file, publish a separately reviewed migration mechanism instead of representing it as a normal
cumulative patch or full rollup.

At startup HQ reads `<update>/content/update.txt`, downloads `releases-chain-v2.txt`, and resolves
the shortest safe path to the newest version. A missing marker means not installed. The marker
changes only after each ZIP is verified and fully installed, making every completed step a retry
checkpoint. Keep `releases-update-only.txt` frozen on the latest full package for older HQ builds,
and keep the older `releases.txt` isolated from update-only releases.

## 5. Curated plugin Workshop

The reachable Workshop carries Aroma plugins only. Revision 28 enables Protarium alongside
Unbound Mod Link and GamePad Mic Redirect. Publish `workshop-plugins.txt` from
this folder at the root of the Unbound repository. HQ displays its last saved catalog or built-in
listings immediately; the user presses **X** in Workshop to fetch a fresh remote catalog. Its active
record format is:

```text
# TYPE|PLUGIN_KEY|NAME|CREATOR|DESCRIPTION|RELEASE_FEED_URL|HOMEPAGE_URL|MANAGED_PATHS
PLUGIN|PROTARIUM|Protarium|Protarium Network|Connect compatible Wii U games to Protarium Network. Installs the paired Inkay Aroma module and plugin. A full console restart is required; Inkay changes network and SSL behavior while active.|https://raw.githubusercontent.com/Protarium-Network/Inkay-GitHub-Release/main/unbound-plugin-release.txt|https://github.com/Protarium-Network/Inkay-GitHub-Release|wiiu/environments/aroma/modules/Inkay-pretendo.wms,wiiu/environments/aroma/plugins/Inkay-pretendo.wps
PLUGIN|UNBOUND_MOD_LINK|Unbound Mod Link|Black Ops 2 Unbound|Loads manifested SD mods without copying them into the installed game.|https://raw.githubusercontent.com/OWNER/bo2-unbound/main/releases-chain-v2.txt|https://github.com/OWNER/bo2-unbound|wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps
PLUGIN|GAMEPAD_MIC_REDIRECT|GamePad Mic Redirect|Tony (tonytrawl)|Redirects the Wii U GamePad built-in microphone for headset-only voice input. Experimental; v0.4.0 adds mute and push-to-talk controls.|https://raw.githubusercontent.com/Unbound-Project/gamepad_mic_redirect/main/release.txt|https://github.com/Unbound-Project/gamepad_mic_redirect|wiiu/environments/aroma/plugins/GamePadMicRedirect.wps
```

Unbound Mod Link is team-owned and uses the same `releases-chain-v2.txt` response that HQ already
downloads for app and game-content updates. Add these special comment directives alongside `#HQ`:

```text
#PLUGIN|UNBOUND_MOD_LINK|REVISION|VERSION|DOWNLOAD_URL|ARCHIVE_SHA256|ARCHIVE_BYTES
#PLUGIN_FILE|UNBOUND_MOD_LINK|wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps|FILE_SHA256|FILE_BYTES
```

Its ZIP must contain exactly the listed WPS path. Publish and verify the immutable ZIP asset first,
then update the shared feed last. Increase `REVISION` whenever any package metadata or WPS byte
changes; never reuse a revision. The leading `#` keeps all three special directive types invisible
to older content parsers.

HQ installs and updates Unbound Mod Link automatically. The plugin itself performs no networking
and does not check for or announce downloadable Unbound content updates. HQ owns all release checks,
prompts, downloads, and installs.

This Unbound-owned catalog is the write authority: `MANAGED_PATHS` is the exact set of ZIP paths HQ
may map beneath `sd:/wiiu/environments/aroma`. The external developer feed can publish newer bytes
for those paths, but cannot add another destination. HQ accepts immediate `.wps` files under
`plugins/` and `.wms` files under `modules/`; it rejects traversal, case aliases, extras, duplicates,
and wrappers around the expected `wiiu/` root.

GamePad Mic Redirect hosts its own release feed at
`https://raw.githubusercontent.com/Unbound-Project/gamepad_mic_redirect/main/release.txt`; its archive may
contain only `wiiu/environments/aroma/plugins/GamePadMicRedirect.wps`. Protarium's active feed is
`https://raw.githubusercontent.com/Protarium-Network/Inkay-GitHub-Release/main/unbound-plugin-release.txt`.
Its exact paired paths and publishing contract are documented in
`PROTARIUM_PLUGIN_PUBLISHING.md`; `protarium-unbound-plugin-release.txt` is a template, not a
replacement for the publisher's live feed.
For any active external repository, activate its developer feed only after the immutable GitHub
Release asset downloads correctly.

Protarium and GamePad Mic Redirect's startup Auto-Updates are opt-in and default to OFF when no
preference is saved. Existing saved ON preferences are retained. Unbound Mod Link remains
automatically managed through the shared release chain. Future plugins added only through the online
catalog can be checked and installed from their detail pages, but cannot enable startup Auto-Update
yet. HQ never adopts a manual install automatically; the user must choose **Replace & Manage** once.
Updates are complete snapshots rather than deltas because these Aroma binaries are small and a full
verified package gives fresh installs and updates the same rollback behavior.

With either external plugin's Auto-Update setting OFF, HQ makes no Workshop-catalog or publisher-feed
request for that plugin during startup. Opening Workshop displays the saved or
built-in listing without waiting for network or local-file inspection. **X** explicitly refreshes
the remote catalog; opening an available plugin's detail page fetches its publisher release feed
and checks its installed files before install/update actions are offered. A cached listing alone is
not proof that its release metadata is current. Supported plugin Auto-Updates remain
synchronous startup work and can extend the time before Home appears. Unbound Mod Link reuses the
release chain, so it adds no second startup request. A catalog-only plugin's detail page marks
startup Auto-Update unavailable instead of offering a switch that HQ would not honor.

Protarium's module and plugin are installed as a verified pair, with a recovery journal and
restoration attempted if the update fails. A full console/Aroma restart is required after either
file changes. Protarium/Inkay changes network and SSL behavior while active; users should read
the publisher's safety notes about reduced SSL verification. HQ's hashes verify matching bytes,
not compatibility with every other networking plugin.

## GitHub's role versus community services

GitHub is suitable for reviewed, team-published static files and release downloads. It is not a
safe in-app database for anonymous comments, votes, or direct user uploads: the Wii U cannot
contain a GitHub write token without exposing it, and GitHub's unauthenticated API is read-only
for this use. See `SERVER_ARCHITECTURE.md` for the recommended community backend and moderation
workflow.
