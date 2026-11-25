# Publishing Protarium for Black Ops 2 Unbound HQ

**Available in HQ revision 28:** Workshop can install Protarium and offer optional startup
Auto-Update. New settings default to OFF; an existing saved ON preference is retained. With
Auto-Update OFF, HQ makes no Protarium publisher-feed request during startup. Opening its
Workshop page checks the publisher feed and installed files on demand.

The reviewed publisher endpoints are:

```text
https://raw.githubusercontent.com/Protarium-Network/Inkay-GitHub-Release/main/unbound-plugin-release.txt
https://github.com/Protarium-Network/Inkay-GitHub-Release
```

HQ treats Protarium as a paired Aroma plugin/module and limits it to:

```text
sd:/wiiu/environments/aroma/modules/Inkay-pretendo.wms
sd:/wiiu/environments/aroma/plugins/Inkay-pretendo.wps
```

`environments` is plural. The paths above match both the current official `wiiu.zip` and Aroma's
actual SD layout.

## Choose the publishing repository

The publisher may use **any public GitHub repository**. Nothing in this procedure depends on a
specific organization or repository name. Before filling the files in, choose these values:

```text
PUBLISHER_GITHUB_OWNER       GitHub user or organization
PUBLISHER_GITHUB_REPOSITORY  Repository name
PUBLISHER_DEFAULT_BRANCH     Branch containing the feed, normally main
```

Add this UTF-8 text file at that repository's root:

```text
unbound-plugin-release.txt
```

Its permanent raw URL will have this form:

```text
https://raw.githubusercontent.com/<PUBLISHER_GITHUB_OWNER>/<PUBLISHER_GITHUB_REPOSITORY>/<PUBLISHER_DEFAULT_BRANCH>/unbound-plugin-release.txt
```

Start from `protarium-unbound-plugin-release.txt` in this folder and replace every placeholder with
the chosen repository, immutable tag, actual hashes, and exact byte counts. The template is not a
copy of the latest live release. If the publisher changes repository or feed location, send the
new URLs to the Unbound maintainer for review and an HQ/catalog update; do not redirect existing
users to an unreviewed publisher.

## Required ZIP layout

Every release is a complete package, not a delta. The ZIP root must be exactly:

```text
wiiu/
  environments/
    aroma/
      modules/
        Inkay-pretendo.wms
      plugins/
        Inkay-pretendo.wps
```

Do not wrap `wiiu/` in another folder. Do not add readmes, changelogs, macOS metadata, duplicate
case variants, or any other entries. HQ rejects undeclared files and unexpected directories.

The filenames are also approved in Unbound's curated `workshop-plugins.txt`. Contact the Unbound
maintainer before renaming either file; changing only the developer feed cannot grant a new write
path.

## Release-feed format

```text
# UNBOUND_PLUGIN_RELEASE_V1
# PLUGIN_KEY|REVISION|VERSION|DOWNLOAD_URL|ARCHIVE_SHA256|ARCHIVE_BYTES
PROTARIUM|NEXT_REVISION|VERSION_OR_TAG|https://github.com/<PUBLISHER_GITHUB_OWNER>/<PUBLISHER_GITHUB_REPOSITORY>/releases/download/<RELEASE_TAG>/wiiu.zip|64_LOWERCASE_HEX|EXACT_ZIP_BYTES
# FILE|ARCHIVE_PATH|SHA256|BYTES
FILE|wiiu/environments/aroma/modules/Inkay-pretendo.wms|64_LOWERCASE_HEX|EXACT_WMS_BYTES
FILE|wiiu/environments/aroma/plugins/Inkay-pretendo.wps|64_LOWERCASE_HEX|EXACT_WPS_BYTES
```

- `REVISION` is a positive, monotonically increasing integer used for update decisions.
- `VERSION` is display text and may match the GitHub tag.
- All URLs must use HTTPS and point to an immutable GitHub Release asset.
- Every SHA-256 is exactly 64 hexadecimal characters.
- Every byte count is the exact file size in bytes, not an estimate.
- Both `FILE` records are mandatory and must match the ZIP exactly.

Never reuse a revision. Never replace an existing tag or asset in place. A changed ZIP, URL,
hash, file list, or corrected metadata requires a new immutable GitHub Release and a higher
revision.

## Publishing checklist

1. Build the `.wms` and `.wps` files and stage the exact `wiiu/environments/aroma/...` tree above.
2. Create `wiiu.zip` from that tree and confirm it contains only the two declared files plus their
   parent directory entries.
3. Calculate the ZIP and individual-file SHA-256 values and exact byte sizes. On Windows:

   ```powershell
   (Get-FileHash .\wiiu.zip -Algorithm SHA256).Hash.ToLowerInvariant()
   (Get-Item .\wiiu.zip).Length
   (Get-FileHash .\wiiu\environments\aroma\modules\Inkay-pretendo.wms -Algorithm SHA256).Hash.ToLowerInvariant()
   (Get-Item .\wiiu\environments\aroma\modules\Inkay-pretendo.wms).Length
   (Get-FileHash .\wiiu\environments\aroma\plugins\Inkay-pretendo.wps -Algorithm SHA256).Hash.ToLowerInvariant()
   (Get-Item .\wiiu\environments\aroma\plugins\Inkay-pretendo.wps).Length
   ```

4. In the chosen repository, create a new immutable GitHub tag/release. Upload `wiiu.zip`, publish
   it, and verify the direct Release asset URL downloads without authentication.
5. Update `unbound-plugin-release.txt` **last**, using the new higher revision, tag/version, direct
   asset URL, hashes, and sizes. Confirm its raw URL and homepage match the reviewed
   `workshop-plugins.txt` record and HQ's built-in listing.
6. Test a fresh install, a managed update, offline startup, Auto-Update OFF, and Auto-Update ON on
   real Wii U hardware.
7. Keep old GitHub Releases available. Do not use Git LFS for release ZIPs.

## User installation and recovery

Open **Workshop > Protarium** and select Install. A pre-existing manual installation is not
silently adopted: the user must confirm **Replace & Manage** before HQ can replace or auto-update
its files. The startup Auto-Update switch is optional and defaults to OFF for users without a saved
preference. A saved ON preference from an older HQ is preserved and resumes checks after upgrading.
Turning it OFF stops publisher-feed checks at startup, not checks requested by opening the page.

HQ downloads the ZIP to its own cache, verifies the declared ZIP size and hash, verifies the exact
entry list plus each file's size/hash, then stages both files before changing either live path. It
keeps a recovery journal and attempts to restore the pair if installation or receipt commit fails.
If recovery cannot finish, follow HQ's recovery message before installing again; do not manually
remove its pending/recovery files. A full console/Aroma restart is required before newly installed
`.wms`/`.wps` code is loaded. Installation cannot hot-switch the currently running network module.

Protarium/Inkay changes console networking and SSL behavior while active. The publisher discloses
reduced SSL verification; read its current safety notes before enabling it. HQ summarizes this on
the plugin page. Hash checks verify that downloaded bytes match the reviewed feed, not that the
network module is harmless or compatible with every other plugin.
