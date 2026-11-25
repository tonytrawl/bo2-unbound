<div align="center">

# Unbound HQ

**Unbound HQ v1.5** is a Wii U homebrew app for keeping **Black Ops II and Call of Duty: Ghosts** Unbound current, installing Workshop content, and managing mods and Aroma plugins.

<br>

[![Latest release](https://img.shields.io/github/v/release/tonytrawl/bo2-unbound?style=for-the-badge&logo=github&logoColor=17130a&label=RELEASE&labelColor=17130a&color=e8a33d)](https://github.com/tonytrawl/bo2-unbound/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/tonytrawl/bo2-unbound/total?style=for-the-badge&label=DOWNLOADS&labelColor=17130a&color=4d8fd6)](https://github.com/tonytrawl/bo2-unbound/releases)
[![Buy me a coffee](https://img.shields.io/badge/SUPPORT%20MY%20WORK-BUY%20ME%20A%20COFFEE%20%E2%98%95-e8a33d?style=for-the-badge&labelColor=17130a&logo=buymeacoffee&logoColor=17130a)](https://buymeacoffee.com/tonytrawl)

</div>

---

HQ finds your supported game and official update, detects its region, language, and internal/USB storage location, and offers the right Unbound files. v1.5 brings dedicated Workshop tabs for **Plugins, Custom Maps, Mods and Custom Packages**, simpler update confirmations, and support ZIPs with Wii U crash logs.

<div align="center">

**Guides:** [Install HQ](#installing-hq) Â· [Workshop](#workshop) Â· [Use local mods](#local-mods-and-mod-manager) Â· [Create mods with the Unbound T6 Mod Tool](README-T6_mod_Tool.md)

**Open source:** [View the app source](source/) Â· [Build HQ](docs/BUILD.md) Â· [GPLv3 license](LICENSE)

</div>

> [!IMPORTANT]
> Install the correct **official game update** first: Black Ops II needs **v128 for USA/Europe** or **v96 or newer for Japan**. Ghosts needs **v112**. HQ does not supply official Nintendo game updates.

> [!WARNING]
> Unbound changes files in your installed Black Ops II/Ghost update. Back up anything you cannot easily replace, and do not interrupt an installation.

---

## What HQ can do

- Switch between **Black Ops II and Ghosts**, with separate game settings and content folders.
- Detect supported regional titles, language folders, and updates on USB or internal NAND.
- Update HQ, install Core and optional map packs, and launch a supported installed or disc copy of the selected game.
- Show available **Core and installed map-pack updates in one Update all prompt**, with sizes and estimated download/install times. The initial install picker stays available for setting things up.
- Install and manage **Plugins, Custom Maps, Mods and Custom Packages** from Workshop.
- Offer the optional **Supply Run** minigame during downloads and installs, including SD installs. Turn it off for a centered progress bar and extra details.
- Export HQ reports and available Wii U crash/DSI logs together as one ZIP.

Game-update files install to the selected regional game's **USB or NAND update**. SD payloads use `sd:/unbound/content/` for BO2 or `sd:/unbound-ghosts/content/` for Ghosts. HQ does not create or modify AOC/DLC titles or their XML files.

---

## Requirements

- A Wii U running the [Aroma environment](https://aroma.foryour.cafe/) and a legally owned Wii U copy of **Call of Duty: Black Ops II** or **Call of Duty: Ghosts**.
- The correct official update: **BO2 v128** (USA/Europe), **BO2 v96 or newer** (Japan), or **Ghosts v112**.
- An SD card accessible from Aroma, an Internet connection for online updates, and enough free space on the SD card and selected game-update storage.

Install the official update and launch the unmodified game once to confirm it works before installing Unbound.

---

## Installing HQ

1. Open the [official Releases page](https://github.com/tonytrawl/bo2-unbound/releases/latest). Download the **Unbound HQ SD ZIP** or `Unbound-HQ.wuhb`.
2. Extract the SD ZIP's `wiiu` folder to the **root of the SD card**, merging folders. If you downloaded the WUHB alone, place it at:

   ```text
   sd:/wiiu/apps/Unbound-HQ/Unbound-HQ.wuhb
   ```

3. Start the Wii U in Aroma and open **Unbound HQ**. Use the game toggle to choose **Black Ops II** or **Ghosts**.
4. Open **Install / Update**, check the detected region, language and USB/NAND destination, then select your Core and optional map packs.

HQ can use matching official ZIPs supplied on SD to shorten installation. Use **Install / Update** for official Core/map-pack installs. Game files still install to USB or NAND; the SD card supplies the package.

After HQ updates itself, relaunch it from the Wii U Menu. After installing or updating Aroma plugins, fully restart the console/Aroma environment.

Do not power off, remove the SD card, disconnect USB storage, or exit HQ while it is downloading or installing.

---

## Workshop

Workshop now has four tabs:

| Tab | What you can do |
| --- | --- |
| **Plugins** | Install, repair and manage Aroma plugins and their update preferences. |
| **Custom Maps** | Browse and install community maps for the selected game. |
| **Mods** | Install, update or reinstall mods for the selected game. |
| **Custom Packages** | Browse your SD folders and choose a ZIP to install yourself. |

Press **X** (**1** on Wii Remote) to refresh listings. Custom Map and Mod entries appear as releases are added. HQ checks package integrity before installing.

### Custom Packages

Place a ZIP anywhere on the SD card, then browse to it in **Workshop â†’ Custom Packages**. Optional starting folders are `sd:/unbound/packages/` for BO2 and `sd:/unbound-ghosts/packages/` for Ghosts.

A package can contain either or both of these folders directly inside its ZIP:

```text
release.zip
  update/
    code/...
    content/...
  sd/
    mods/example/...
```

- `update/` installs into the selected game's USB/NAND update.
- `sd/` installs into `sd:/unbound/content/` for BO2 or `sd:/unbound-ghosts/content/` for Ghosts.

Review the files and destinations before confirming. The source ZIP stays on the SD card. Custom packages keep existing official release-version records; use **Install / Update** for official Core/map-pack installations.

### GamePad Mic Redirect

GamePad Mic Redirect is an optional, experimental plugin that routes the Wii U GamePad's built-in microphone to compatible games that normally expect headset voice input. Its release includes **mute** and **push-to-talk** controls. Read its Workshop page and [publisher notes](https://github.com/Unbound-Project/gamepad_mic_redirect) before installing.

Its **Auto-Update** setting starts **OFF**. When off, open its Workshop page to check manually. After HQ installs or explicitly adopts the plugin, you may turn Auto-Update on. HQ asks before taking over an existing manual plugin installation.

Aroma plugin packages install to the SD card; they do not replace game-update files.

---

## Local mods and Mod Manager

Put each local mod in its own folder, with a `modload.txt` file:

- **BO2:** `sd:/unbound/content/mods/<mod-folder>/`
- **Ghosts:** `sd:/unbound-ghosts/content/mods/<mod-folder>/`

For example:

```ini
name=Diner Survival
description=A custom survival experience for Diner.
author=Example Author
```

The [Unbound T6 Mod Tool guide](README-T6_mod_Tool.md) explains how to create, edit, and validate loader-ready BO2 mods on Windows. Copy the finished mod folder to the matching SD path above, or install a packaged mod through Workshop.

Mod Manager shows SD and console copies in one list, with each mod's name, author, and description.
---

## Controls

The Wii U GamePad, Wii U Pro Controller, Classic Controller/Classic Controller Pro, and Wii Remote can navigate HQ. The footer shows what each button does on the current screen.

| Control | Action |
|---|---|
| D-pad | Navigate cards and lists; scroll long text with Up/Down |
| A | Select or confirm |
| B | Back or cancel |
| X, or 1 on Wii Remote | Refresh Workshop or manage/delete a selected mod when shown in the footer |
| Plus | Exit HQ |

---

## Troubleshooting

- **Official update not found:** Check the selected game's update requirement, confirm game/update regions match, and connect the USB drive before starting HQ if the update is on USB.
- **Network or HTTP error:** Test the Wii U Internet connection. Saved listings may still appear when an online check fails.
- **Checksum or size mismatch:** Retry, then report a repeat failure with the exact error.
- **Not enough free space:** Free space on the device HQ names. The SD download cache and game-update storage both need room.
- **Interrupted installation:** Reopen HQ and finish its saved installation/repair before launching the game.
- **A local mod is missing:** Check its game-specific SD folder and readable `modload.txt`, and restart the game after changing mod-loading settings.

### Export a support ZIP

Open **Support â†’ Export ZIP**. HQ saves:

```text
sd:/wiiu/apps/Unbound-HQ/support-report.zip
```

The ZIP includes HQ diagnostics, available install/performance logs, and the Wii U crash/DSI logs from `/storage_slc/sys/logs/`. Original console logs stay in place. The export summary lists any unavailable or skipped files.

When reporting a bug, share this ZIP with the HQ version, game region, language, USB/NAND location, and what happened just before the issue.

---

## Source code

The **full Wii U application source is in this repository**: [source/](source/), [include/](include/), [tests/](tests/), [tools/](tools/), and the build assets.

The application source uses the [GNU GPLv3 license](LICENSE) (version 3 only). Copyright and author credit for Tony (tonytrawl) and contributors are preserved in [NOTICE](NOTICE). See [Build HQ](docs/BUILD.md) for build and verification instructions. Bundled third-party assets retain their own licenses and attribution files. Earlier MIT releases retain their original license; see [the legacy notice](LICENSES/MIT-legacy.txt).

---

## Legal notice

Unbound HQ is an unofficial, fan-made homebrew project. It is not affiliated with, endorsed by, or sponsored by Activision, Treyarch, Nintendo, Pretendo Network, or any of their subsidiaries.

Call of Duty, Black Ops II, Wii U, and related names and assets belong to their respective owners. You are responsible for using legally obtained game software and content.
