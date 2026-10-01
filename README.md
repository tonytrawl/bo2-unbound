# Black Ops 2 Unbound HQ

**Black Ops 2 Unbound HQ** is a Wii U homebrew app for keeping Black Ops II Unbound current, managing local mods, and installing curated Aroma plugins.


[![Latest release](https://img.shields.io/github/v/release/tonytrawl/bo2-unbound?style=for-the-badge&logo=github&logoColor=17130a&label=RELEASE&labelColor=17130a&color=e8a33d)](https://github.com/tonytrawl/bo2-unbound/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/tonytrawl/bo2-unbound/total?style=for-the-badge&label=DOWNLOADS&labelColor=17130a&color=4d8fd6)](https://github.com/tonytrawl/bo2-unbound/releases)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Buy me a Coffee](https://img.shields.io/badge/Support%20My%20Work-Buy%20me%20a%20coffee%20%E2%98%95-chocolate?style=plastic)](https://buymeacoffee.com/tonytrawl)

HQ finds your supported Black Ops II game and official update, detects its region, language, and internal/USB storage location, and offers the right Unbound files. It can update the HQ app itself, apply full or patch game-content releases, launch the game, and manage mods from one place.

**Guides:** [Install HQ](#installing-hq) · [Use local mods](#local-mods-and-mod-manager) · [Create mods with the Unbound T6 Mod Tool](README-T6_mod_Tool.md)

> [!IMPORTANT]
> Install the newest **official** Black Ops II update first: **v128 for USA and Europe**, or **v96 for Japan**. HQ does not supply the official game update or DLC.

> [!WARNING]
> Unbound is still in beta and changes files in your installed Black Ops II update. Back up anything you cannot easily replace, and do not interrupt an installation.

## New in the next Unbound content package

### Theater Mode

Local recording and playback is back into the game's familiar **Theater → Select Film** flow. Record supported hosted matches, browse and replay completed films, save edited clips, and remove individual recordings. Multiplayer and Zombies films stay separated. This is a local film library, not cloud sharing or video export.

### Performance and mixed lobbies

Project comparison tests measured approximately **11% higher FPS**, **12% lower overall CPU usage**, and **28% lower usage on the busiest CPU core** with the new game-code optimizations. These are approximate measured results, not a guaranteed improvement in every map, mode, or console setup.

Host and client side **Lua/LUI compatibility fixes** address the different Lua version rejection that could stop a player without Unbound from joining an Unbound-hosted lobby. Mixed lobbies still need compatible maps and gameplay assets on every player's console; this fix does not supply missing custom content.

## What HQ can do

- Detect supported USA, European, and Japanese Black Ops II titles, the selected language, and whether the official update is on internal storage or USB.
- Check for a newer HQ app before checking game content. A verified app update returns you to the Wii U Menu so you can relaunch it.
- Launch a supported installed or disc copy of Black Ops II. A missing game or update is reported without crashing the app.
- Manage SD and console mod copies together and browse the curated **Plugin Workshop**.

HQ installs Unbound game content into the registered **update title**, under `update/code/` and `update/content/`. Files staged in `update/content/@language/` go to the detected English, French, Spanish, Italian, German, or Japanese folder. HQ does **not** create or modify AOC/DLC titles or their XML files.

The Workshop currently offers Aroma plugins. Community mod uploads, ratings, and comments are not enabled yet.

## Requirements

- A Wii U running the [Aroma environment](https://aroma.foryour.cafe/) and a legally owned Wii U copy of **Call of Duty: Black Ops II**.
- The newest official Black Ops II update: **v128** in USA/Europe or **v96** in Japan.
- An SD card accessible from Aroma, Internet access for online updates, and sufficient free space on both the SD card and the device holding the game update.
- Aroma's existing `ContentRedirectionModule.wms` if you want to load mods from SD with Unbound Mod Link. HQ does not replace that shared module.

Install the official update from a legitimate source and launch the unmodified game once to confirm it works before installing Unbound.

## Installing HQ

1. Open the [official Releases page](https://github.com/tonytrawl/bo2-unbound/releases). Download the **HQ application** `BO2-Unbound-HQ.wuhb` or its SD-card package. A file named `unbound-universal-...zip` is a game-content package, **not** the app.
2. If you downloaded the WUHB alone, copy it to:

   ```text
   sd:/wiiu/apps/BO2-Unbound-HQ/BO2-Unbound-HQ.wuhb
   ```

   If you downloaded an SD-card package, extract it to the SD-card root and confirm the WUHB ends up at that same path.
3. Insert the SD card, start the Wii U in Aroma, and open **Black Ops 2 Unbound HQ**.

Only the `.wuhb` is required for normal Aroma use; development `.elf` and `.rpx` files do not belong on the SD card. The first self-updating HQ build must be installed manually. Later HQ revisions can update the app for you. After HQ updates itself, relaunch it from the Wii U Menu. After installing or updating an Aroma plugin, fully restart the console/Aroma environment so its new code loads.

On each run HQ checks the shared `releases-chain-v2.txt` feed, locates the game and official update, and compares `update/content/update.txt` with the available Unbound releases. It asks before downloading game content. A missing version marker means a first install; HQ writes the marker only after a package finishes successfully. Do not manually create or edit `update.txt` to bypass the installer.

Do not power off, remove the SD card, disconnect USB storage, or exit HQ while it is downloading or installing.

## Plugin Workshop

Open **Workshop** from HQ to see the curated plugin listings. The page opens immediately from saved listings, or HQ's included listings if none have been saved. Press **X** (**1** on a Wii Remote) to refresh the catalog. Open a plugin's page to check its current publisher release and the files installed on your SD card. A saved listing is useful offline, but does not prove that its release is current.

### Unbound Mod Link

Unbound Mod Link lets Black Ops II load manifested mods directly from `sd:/unbound/content/mods` at runtime. You can keep mod folders on the SD card instead of repeatedly copying them to the game's internal or USB storage. It overlays matching files rather than replacing the entire game content folder. The switch in Mod Manager controls whether linking is active; restart the game after changing it.

HQ automatically checks for verified Mod Link plugin updates using the same shared feed it already downloads for HQ and game content. The runtime plugin itself makes **no** network requests and does not check for game-content updates while you play. Its package contains only the Unbound plugin; it does not overwrite Aroma's shared content-redirection module. If notifications are available, it can briefly show when linking succeeds.

### GamePad Mic Redirect

GamePad Mic Redirect is an optional, experimental plugin that routes the Wii U GamePad's built-in microphone to compatible games that normally expect headset voice input. Its current release includes **mute** and **push-to-talk** controls. Read its Workshop page and [publisher notes](https://github.com/tonytrawl/gamepad_mic_redirect) before installing.

Its **Auto-Update** setting starts **OFF**. When OFF, HQ does not contact the microphone plugin's publisher feed during startup; opening its Workshop page still lets you check manually. After HQ installs or explicitly adopts the plugin, you may turn Auto-Update ON from that page. HQ then checks for verified newer releases at startup. An existing manual plugin file is never silently taken over: HQ requires a **Replace & Manage** confirmation. Plugins added only through future online catalog entries can be checked from their pages, but do not offer startup Auto-Update yet.

No Workshop plugin package writes into the Black Ops II game-update folders.

## Local mods and Mod Manager

Put each local mod in its own folder at `sd:/unbound/content/mods/<mod-folder>/`, with a `modload.txt` file. For example:

```ini
name=Diner Survival
description=A custom survival experience for Diner.
author=Example Author
```

The [Unbound T6 Mod Tool guide](README-T6_mod_Tool.md) explains how to create, edit, and validate loader-ready mods on Windows. Copy the finished mod folder to the SD path above. HQ can also offer to migrate older `sd:/unbound/mods` or `sd:/unbound/mod` folders; it does not silently overwrite a conflicting folder name.

Mod Manager shows SD and console copies in one list, with each mod's name, author, and description. Press **Minus** there to toggle Unbound Mod Link. The ON/OFF bubble shows its status, not an extra button. When matching folders exist in both places, HQ warns that the SD version takes priority while Mod Link is enabled, but console-only files can still appear in the merged view. You can review and remove the redundant console copy from Mod Manager.

Manual SD-to-console and console-to-SD copying remains available for compatibility, but Mod Link is the recommended everyday setup. Follow the on-screen confirmations carefully when copying or deleting. A console-to-SD move verifies the new copy before offering to remove the original. Deleting a console copy does not delete the SD copy.

## Controls

Touch input is intentionally disabled. The Wii U GamePad, Wii U Pro Controller, Classic Controller/Classic Controller Pro, and Wii Remote can navigate HQ. The footer shows what each button does on the current screen.

| Control | Action |
|---|---|
| D-pad | Navigate cards and lists; scroll long text with Up/Down |
| A | Select or confirm |
| B | Back or cancel |
| Minus in Mod Manager | Turn Mod Link on or off |
| X, or 1 on Wii Remote | Refresh Workshop or manage/delete a selected mod when shown in the footer |
| Plus | Exit HQ |

## Troubleshooting

- **Official update not found:** Check for v128 (USA/Europe) or v96 (Japan), confirm the game and update match regions, and connect USB storage before starting HQ if the update is on USB.
- **No Unbound version found:** HQ expects `update/content/update.txt`. If it is missing, HQ should offer a full install; do not create the file yourself.
- **Network or HTTP error:** Test the Wii U Internet connection and check GitHub on another device. A failed online check is not the same as being up to date; saved news and Workshop listings may still appear.
- **Checksum or size mismatch:** Do not bypass it. Retry, then report a repeat failure with the exact error.
- **Not enough free space:** Free space on the device HQ names. Both the SD download cache and the actual game-update storage need room.
- **Missing BSP or fastfile:** Verify that the official update is current, installation finished without errors, and the release has all required map files under the update tree rather than an AOC path.
- **Mod Link does not load an SD mod:** Check that the mod is under `sd:/unbound/content/mods/<mod-folder>/`, has a readable `modload.txt`, Mod Link is ON, and Aroma's content-redirection module is present. Restart the game after changing the switch; restart the console/Aroma after a plugin install or update.

For a bug report, include the HQ version and revision, game region, official update version, internal/USB location, language, and exact on-screen message.

## Legal notice

Black Ops 2 Unbound HQ is an unofficial, fan-made homebrew project. It is not affiliated with, endorsed by, or sponsored by Activision, Treyarch, Nintendo, Pretendo Network, or any of their subsidiaries.

Call of Duty, Black Ops II, Wii U, and related names and assets belong to their respective owners. You are responsible for using legally obtained game software and content. Do not distribute copyrighted game files, official DLC, encryption keys, tickets, or other protected material through this project. Use this software at your own risk.
