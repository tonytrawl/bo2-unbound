# Black Ops 2 Unbound HQ

**Black Ops 2 Unbound HQ** is a Wii U homebrew application for installing and managing
Black Ops II Unbound releases and local community mods.

HQ detects the game region, language, and storage device automatically. It checks the
installed Unbound version against the current GitHub release, downloads the correct universal
package, verifies it, and installs its files into the registered Black Ops II update title.

**Project guides:** [Install and use Unbound HQ](#installing-hq) ·
[Create mods with the Unbound T6 Mod Tool](README-T6_mod_Tool.md)

> [!IMPORTANT]
> Black Ops II must have its newest official game update installed before you use HQ:
> **v128 for USA and Europe**, or **v96 for Japan**.

> [!WARNING]
> This project is currently in beta and modifies files inside the installed game update.
> Keep backups of anything you cannot easily replace, and do not interrupt an installation.

## Features

- Checks for new Unbound releases every time HQ starts.
- **What's New** page with up to date updates on the project.
- Browses and manages mods through the Mod Manager.
- Works with the companion [Unbound T6 Mod Tool](README-T6_mod_Tool.md) for creating and
  validating loose mods on Windows.

The online **Workshop** is currently disabled and will be enabled in a future release. Local
mods remain available through the Mod Manager.

## Requirements

- A Wii U running the [Aroma environment](https://aroma.foryour.cafe/)
- A legally owned Wii U copy of **Call of Duty: Black Ops II**
- The newest official Black Ops II game update
- An SD card accessible from Aroma
- An Internet connection 
- Enough free space on both the SD card and the storage device containing the game update

### Required official game version

| Game region | Required official update |
|---|---:|
| USA | **v128** |
| Europe | **v128** |
| Japan | **v96** |

Install the official update through a legitimate source, then launch the unmodified game once
to confirm that it works before installing Unbound. HQ installs Unbound content; it does not
download or replace Nintendo's official Black Ops II update.

## Installing HQ

### Using the SD-card package

1. Open the repository's [Releases page](https://github.com/tonytrawl/bo2-unbound/releases).
2. Download the release asset identified as **BO2-Unbound-HQ**.
   Its filename should begin with `BO2-Unbound-HQ`, not `unbound-universal`.
3. Extract the package directly to the root of the SD card.
4. Confirm that the resulting file is located at:

   ```text
   sd:/wiiu/apps/BO2-Unbound-HQ/BO2-Unbound-HQ.wuhb
   ```

5. Insert the SD card, start the console with Aroma, and open **Black Ops 2 Unbound HQ**.

> [!NOTE]
> Files named `unbound-universal-<version>-full.zip` are content packages consumed
> automatically by HQ. Users should not extract those packages onto the SD card manually.

## First run and automatic updates

Every time HQ starts, it:

1. Initializes the network and available storage.
2. Searches for the Black Ops II base game and official update.
3. Determines whether you are on the newest update.
4. Prompts before downloading or installing anything.

Do not power off the console, remove the SD card, disconnect USB storage, or close HQ during a
download or installation.

## Creating mods with the Unbound T6 Mod Tool

This repository also includes the
[Unbound T6 Mod Tool guide](README-T6_mod_Tool.md). The Unbound T6 Mod Tool is a separate
Windows editor and mod-folder builder for creating loader-ready Black Ops II Wii U loose mods.

The tool can:

- Create a correctly structured mod folder and edit its `modload.txt` metadata.
- Scaffold Multiplayer and Zombies scripts and gametypes.
- Compile GSC or Lua source for Wii U or PC.
- Convert supported scripts between Wii U and PC bytecode formats.
- Edit CFG settings and CSV StringTables.
- Import supported images as loose Wii U GX2 texture replacements.
- Preview GX2 textures and validate mods against known loader limits.

The roles of the two applications are different:

1. Use the **Unbound T6 Mod Tool** on a Windows PC to create, edit, and validate the mod.
2. Copy the finished mod folder to `sd:/unbound/mods/<mod-folder>/`.
3. Use **Unbound HQ** on the Wii U to inspect the manifest and copy the mod into the game's
   update `content/mods` directory.

See the [complete T6 Mod Tool documentation](README-T6_mod_Tool.md) for installation, editor
features, folder layout, script lanes, texture replacement, validation limits, and troubleshooting.

## Using the Mod Manager

Place every local mod in its own directory under:

```text
sd:/unbound/mods/
```

For example:

```text
sd:/unbound/mods/Diner Survival/
├── modload.txt
└── additional mod files...
```

The preferred `modload.txt` format is:

```ini
name=Diner Survival
description=A custom survival Experience for Diner.
author=Example Author
```

HQ also accepts three plain lines in name, description, author order, or one
`name;description;author` line.

To install a local mod:

1. Open **Mod Manager** from the HQ home screen.
2. Use Left/Right on the D-pad to switch between **SD Card** and **Installed**.
3. Highlight the desired SD-card mod.
4. Press **A** to copy the complete folder into the game's update `content/mods` directory.

Switch to **Installed** to inspect installed mods. Press **X** and confirm to remove the selected
mod's entire installed folder. Removing an installed copy does not delete its source folder from
the SD card.

## Controls

The footer on each screen shows the controls available for that view.

| Control | Action |
|---|---|
| D-pad | Navigate cards, menus, lists, and keyboard keys |
| Up/Down | Select items or scroll long content |
| Left/Right | Move horizontally or change the Mod Manager source |
| A | Select or confirm |
| B | Go back or cancel |
| X | Delete the selected mod from the Installed view |
| + | Exit HQ safely |

Touch input is intentionally disabled. Use the Wii U GamePad or another supported controller.

## Launching Black Ops II

HQ can hand off to the game when it detects:

- A supported base game installed on internal memory
- A supported base game installed on USB storage
- A supported inserted game disc

If no usable copy is found, HQ displays a warning and continues running. A missing game or disc
does not crash the application.

## Troubleshooting

### The official update is not found

- Confirm USA/Europe is updated to **v128**, or Japan is updated to **v96**.
- Launch the unmodified game once and confirm that its update loads.
- Reconnect USB storage before opening HQ if the official update is installed on USB.
- Make sure the game and its update belong to the same region.

### `Release feed: Server returned HTTP 404`

HQ could not find its update-only manifest. Check network connection. Releases may briefly cache a 404 after a new file
is added if you are early to the download; wait a minute, and restart HQ.

### HQ reports that Unbound is current, but you have not installed it

- Confirm that you are running the newest HQ WUHB.
- Confirm that HQ detected the intended Black Ops II update installation.

Do not manually create `update.txt` to bypass the installer. HQ writes it only after a successful
installation.

### Checksum mismatch

The downloaded ZIP does not match the SHA-256 value published in the release feed. Do not bypass
this warning. Retry the download, and report it if the error continues.

### Not enough free space

HQ needs room for the downloaded archive on the SD card and for the unpacked files on the storage
device containing the game update. Free space on the indicated device and retry. HQ reserves
additional safety space instead of filling a device completely.

### Network or host-resolution error

- Test the Wii U Internet connection from System Settings.
- Confirm that GitHub is available on another device.
- Check custom DNS or router filtering.
- Retry after the console has fully connected to the network.

### A map reports a missing BSP or fastfile

- Confirm that the installation completed without an error.
- Confirm that the official game update is current for the region.
- Confirm that the release contains every required FF, IPAK, and sound file.
- Current packages must place map content under the update tree, not an AOC path.

## Download integrity and safety

HQ downloads packages over HTTPS and verifies each complete archive against the SHA-256 value in
the release feed before installation. It also rejects unsafe ZIP paths and backs up replaced files
to the HQ recovery area on the SD card.

Download HQ and its packages only from the official repository:

<https://github.com/tonytrawl/bo2-unbound>

Do not use manifests or packages from an untrusted fork. Only distribute content you have the
right to share.

## Project status

Black Ops 2 Unbound HQ is under active development. The local Mod Manager and core updater are
available now. The integrated Workshop, community ratings, comments, and user uploads remain
disabled until the supporting moderated service is ready.

Bug reports should include:

- HQ version
- Game region and official update version
- Game/update storage location
- Selected language
- Exact on-screen error message

## Legal notice

Black Ops 2 Unbound HQ is an unofficial, fan-made homebrew project. It is not affiliated with,
endorsed by, or sponsored by Activision, Treyarch, Nintendo, Pretendo Network, or any of their
subsidiaries.

Call of Duty, Black Ops II, Wii U, and related names and assets belong to their respective owners.
You are responsible for using legally obtained game software and content.
