# kh-pkg-overlay

An [exlaunch](https://github.com/shadowninja108/exlaunch) plugin that loads mods for the Nintendo Switch release of Kingdom Hearts HD 1.5+2.5 ReMIX without rebuilding the game's multi-GB `.pkg` archives. You put the modded files on the SD card as loose files, and the game picks them up instead of the originals.

It works with [KHPCPatchManager](https://github.com/AntonioDePau/KHPCPatchManager) patches (`.kh1pcpatch`, `.kh2pcpatch`) made for the PC version. Tested with game version 1.0.0 and the German voice patches for Kingdom Hearts Final Mix and Kingdom Hearts II Final Mix.

Made with Claude for my own use case, so I'm not interested in taking feature requests.

## Requirements

- A Switch running Atmosphère
- devkitPro with devkitA64 and libnx, to build the plugin
- [uv](https://docs.astral.sh/uv/), to run the tools in `tools/`
- The game's `main.npdm` for each game you mod, taken from that game's ExeFS

## Build

```sh
cd exlaunch
DEVKITPRO=/opt/devkitpro ./exlaunch.sh build
```

The plugin is `exlaunch/deploy/subsdk9`. Ignore the `main.npdm` next to it.

## Install a mod

Each game in the collection has its own program ID:

| Program ID | Game |
| --- | --- |
| `010090C022978001` | Kingdom Hearts Final Mix |
| `010090C022978003` | Kingdom Hearts II Final Mix |

The steps below use Kingdom Hearts II; for Kingdom Hearts Final Mix, use `010090C022978001` and that game's `main.npdm`.

1. Unpack the patch onto the SD card:

   ```sh
   tools/pcpatch-to-loose.py "German voices KH2 2.1.kh2pcpatch" -x '*/title.2ld/*' \
       -o sd/atmosphere/contents/010090C022978003/romfs
   ```

   `-x` skips files by pattern; here it leaves out the title screen, which differs on Switch. With several patches, list them all; later ones win.

2. Copy the plugin and a patched copy of the game's `main.npdm`:

   ```sh
   cp exlaunch/deploy/subsdk9 sd/atmosphere/contents/010090C022978003/exefs/
   tools/npdm-add-svcs.py kh2-main.npdm sd/atmosphere/contents/010090C022978003/exefs/main.npdm
   ```

   The plugin needs a few system calls the game doesn't use; `npdm-add-svcs.py` enables them.

3. Optional: replaced videos. Some mods ship the cutscene videos as a separate download made for the PC version, with `EPIC` or `STEAM` as the top folder (e.g. `STEAM/juefigs/KH2ReSource/zmovie/en/opn.mp4`). The videos don't go through the plugin. Rename that top folder to `Switch` and copy it into `romfs`, so the file ends up at `romfs/Switch/juefigs/KH2ReSource/zmovie/en/opn.mp4`.

4. On macOS, delete the `._*` files that macOS creates on the FAT32 card: `dot_clean /Volumes/Switch-SD`.

For a voice patch with videos, the folder looks like this:

```
atmosphere/contents/010090C022978003/
├── exefs/
│   ├── main.npdm
│   └── subsdk9
└── romfs/
    ├── kh-mods/
    │   ├── original/
    │   │   └── voice/us/
    │   │       ├── battle/
    │   │       │   ├── tt0_roxas.win32.scd
    │   │       │   └── tt0_sora.win32.scd
    │   │       └── event/
    │   │           ├── tt00101go.win32.scd
    │   │           └── tt00102sr.win32.scd
    │   └── remastered/
    │       └── obj/B_CA010.a.us/
    │           └── voice/battle/us/barbossa
    └── Switch/
        └── juefigs/KH2ReSource/zmovie/en/
            ├── opn.mp4
            └── ena.mp4
```

A file in `kh-mods/original/` replaces the game file with the same path: `kh-mods/original/voice/us/battle/tt0_sora.win32.scd` replaces `voice/us/battle/tt0_sora.win32.scd`. A file in `kh-mods/remastered/<game file>/` replaces one of the HD textures or sounds that the HD release attached to that game file.

## Limits

- Loose files can only replace assets the game already has; a patch that adds new files needs the delta mode below.
- A replaced model or BAR file with a different internal layout can show the wrong HD textures.
- If a game update changes the code the plugin looks for, loose files stop working until the plugin is updated. The delta mode keeps working.

## Delta mode

The plugin can also read a `.pkg` archive as the original followed by a `.pkg.delta` file that holds only the changed assets, together with a patched `.hed`. [kh-pcpatch](https://github.com/WiiDatabase/kh-pcpatch) creates these files from the game's original archives:

```sh
kh-pcpatch.py apply kh2-romfs "German voices KH2 2.1.kh2pcpatch" -x '*/title.2ld/*' \
    --delta -o sd/atmosphere/contents/010090C022978003/romfs
kh-pcpatch.py verify sd/atmosphere/contents/010090C022978003/romfs --base kh2-romfs
```

The `.hed` and `.pkg.delta` files go into `romfs/` next to `kh-mods/`; plugin and `main.npdm` are the same as above. After a game update, build them again from the new archives.

## License

GPL-2.0, like exlaunch, which is included in `exlaunch/` with these changes:

- `source/lib/init/crt0.s`: the module starts with `b +8` (needed by the rtld of SDK 21 and later)
- `misc/specs/module.specs`: `--hash-style=both`
- `source/rtld.hpp`: `g_LookupGlobalManualFunctionPointer` is weak (newer SDKs don't export it)
- `source/lib/hook/nx64/hook_impl.cpp`: builds with current GCC
- `source/nn/fs/fs_types.hpp`: `DirectoryEntry` uses the SDK layout
