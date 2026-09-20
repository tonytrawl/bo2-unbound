# Unbound T6 Mod Tool

A Windows editor and mod-folder builder for loose **Call of Duty: Black Ops II** Wii U mods.
It creates the loader-ready folder structure, edits manifests and tables, compiles scripts,
converts Wii U and PC bytecode, and imports Wii U texture replacements from common image
formats.

**Created by [tonytrawl](https://github.com/tonytrawl).**

## Contents

- [Features](#features)
- [Public build scope](#public-build-scope)
- [Requirements](#requirements)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Mod folder layout](#mod-folder-layout)
- [Script lanes](#script-lanes)
- [Script editor](#script-editor)
- [Gametype tools](#gametype-tools)
- [Manifest editor](#manifest-editor)
- [CFG and CSV editors](#cfg-and-csv-editors)
- [Texture replacement](#texture-replacement)
- [Validation and loader limits](#validation-and-loader-limits)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)

## Features

- Create a loader-ready mod folder without assembling its directories by hand.
- Edit existing loose mods and their `modload.txt` manifests.
- Create Multiplayer and Zombies gametype scaffolds.
- Create register-only scripts under `scripts/`.
- Create startup scripts under `custom_scripts/` that automatically run `main()`.
- Insert existing GSC files and convert them to Wii U byte order.
- Open and edit GSC, CSC, Lua/HKS, CFG, and CSV StringTable files.
- Compile plain GSC or Lua source for Wii U or PC.
- Convert individual files or batches between Wii U big-endian and PC little-endian.
- Import IWI, DDS, PNG, JPEG, BMP, TGA, GIF, TIFF, and WebP images as Wii U GX2 textures.
- Preview `.gx2` files and validate the mod against known loader limits.
- Preserve the original file with a `.bak` backup on the first overwrite.
- Open a complete in-app guide with **Help** or **F1**.

## Requirements

- Windows 10 or Windows 11, 64-bit.
- A legally obtained copy of Call of Duty: Black Ops II for Wii U.
- Project Unbound installed on a Wii U or Cemu

## Installation

1. Download `Unbound_T6_Mod_Tool_Public` from the project release.
2. Put it in any writable folder.
3. Run the executable.

Windows may show a SmartScreen warning for an unsigned community executable. Verify the
release checksum before running it if one is supplied with the download.

## Quick start

1. Open the tool.
2. Select **Create Mod**.
3. Choose the parent `content\mods` directory.
4. Enter the new mod folder name. The folder name becomes the mod id.
5. Edit `modload.txt` and add scripts, a gametype, tables, or replacement images.
6. Select each edited file and use **File > Save** or press **Ctrl+S**.
7. Resolve errors shown in the validation panel.
8. Start the game, open the Mods menu, and select the mod.
```

You can also select **Edit Mod** and point the tool at an existing mod folder.

## Mod folder layout

```text
content/mods/<mod-folder>/
├── modload.txt
├── scripts/
│   ├── maps/
│   ├── mp/
│   └── images/
└── custom_scripts/
    └── scripts whose main() function runs automatically
```

The mod folder name is the mod id. Only `scripts/` and `custom_scripts/` are loader asset
roots. Placing an arbitrary asset folder beside them does not register its contents.

## Script lanes

| Folder | Loader behavior | Use it for |
| --- | --- | --- |
| `scripts/` | Registers the asset without automatically calling `main()` | Replacements and assets the game already requests |
| `custom_scripts/` | Registers the asset and automatically calls `main()` | New startup hooks and independent custom scripts |

A script placed in the wrong lane can load without producing any visible result. Use
`custom_scripts/` when the script needs the loader to start it.

## Script editor

The **View** selector provides three representations:

### Source

Decompiles compiled bytecode into readable source. Saving recompiles the source. If the
decompiler could not recover every function, the tool warns because saving would omit the
missing functions.

### Assembly

Shows the precise GSC representation. Supported GSC files can round-trip byte-for-byte.
Compiled Lua assembly is read-only because the tool does not have a Lua assembly writer.

### Text

Used for plain source files. Select **Tools > Compile this text** or **Convert this file** to
produce Wii U or PC bytecode.

### Useful shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+O` | Open a script file |
| `Ctrl+S` | Save the active file or mod editor |
| `Ctrl+F` | Find inside the script editor |
| `F1` | Open the complete in-app help window |

## Gametype tools

Select **+ Gametype** in a mod to generate the required server script, client script, and CFG
paths for Multiplayer or Zombies. The generated scripts are compiled Wii U bytecode rather
than plain text saved with a `.gsc` extension.

The tool places each mode in its expected path, including the different Zombies server and
settings directories.

## Manifest editor

Select `modload.txt` in the mod tree to edit:

| Field | Purpose |
| --- | --- |
| Name | Display name in the Mods menu |
| Description | User-facing mod description |
| Author | Mod author credit |
| Command | Optional loader command; may be repeated |
| Reload | Requests teardown/reload behavior after selection |
| Scope | Applies the mod to `map`, `frontend`, or `both` |

Unknown manifest fields are retained when the file is saved.

## CFG and CSV editors

### CFG

Gametype CFG files open as editable setting grids. The editor can add or remove settings and
provides stock presets as a starting point.

### CSV StringTables

CSV files open as rectangular grids. The editor preserves quoted commas, escaped quotes, and
multiline cells. Adding or removing a column changes every row so the table remains valid for
the engine.

## Texture replacement

The public image workflow creates **loose replacements for existing game images**.

1. Select **+ Loose texture replacement**.
2. Choose an IWI, DDS, PNG, JPEG, BMP, TGA, GIF, TIFF, or WebP file.
3. Enter the exact stock GfxImage asset name.
4. The converted file is written to `scripts/images/<asset-name>.gx2`.

The replacement should match the stock image's dimensions and texture format. Arbitrary new
image names do not create Materials or references to those images. A bad name or incompatible
surface can produce a missing texture or an in-game crash.

Normal images with transparency are encoded as BC3. Fully opaque images are encoded as BC1.

## Validation and loader limits

The validation panel reports assets, auto-run scripts, image count, shared arena use, and
layout problems. Current checks include these loader limits:

| Limit | Value |
| --- | ---: |
| Queued directories | 32 |
| Relative asset path | 128 bytes |
| Auto-run `custom_scripts` | 32 |
| File read size | 128 KiB per file |
| Shared registration arena | 256 KiB |

GX2 image payloads do not consume the shared registration arena in the same way as ordinary
registered files, but their names and surface compatibility still matter.

## Troubleshooting

### The mod does not appear

- Confirm the folder is directly inside `content/mods/`.
- Confirm `modload.txt`, `scripts/`, and `custom_scripts/` are inside the mod folder.
- Check the validation panel for invalid paths or limits.
- Confirm the installed RPL includes the loose mod loader.

### A custom script appears but does not run

- Put new startup scripts under `custom_scripts/`.
- Confirm the script contains `main()`.
- Confirm the file is compiled Wii U bytecode. Plain text with a `.gsc` extension will not run.

### A replacement script has no effect

Files under `scripts/` are registered but not started automatically. Their engine asset path
must exactly match the script the game requests.

### A texture is missing or crashes the game

- Use the exact existing stock image name.
- Match the original dimensions and format.
- Test one replacement at a time on Cemu before moving it to hardware.

### Source view reports an incomplete decompile

Do not save the incomplete Source view unless you accept losing the refused functions. Use
Assembly for supported byte-exact GSC editing, or use conversion without editing the script.

### The Mods menu freezes or displays damaged text

Validate `modload.txt`, avoid malformed CSV quoting, and stay within the loader's path, file,
directory, and arena limits.

## Credits

Created by **[tonytrawl](https://github.com/tonytrawl)**.

This is an independent community project and is not affiliated with Activision, Treyarch,
Nintendo, or the Cemu project. Product names and trademarks belong to their respective
owners.
