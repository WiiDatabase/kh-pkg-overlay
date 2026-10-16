# kh-pkg-overlay

An [exlaunch](https://github.com/shadowninja108/exlaunch) plugin for the Nintendo Switch release of Kingdom Hearts HD 1.5+2.5 ReMIX. It lets the game read each `.pkg` archive as the original file from the game's RomFS followed by a `.pkg.delta` file from LayeredFS. A mod then ships the patched `.hed` and a delta with only the changed assets, instead of rewritten archives of several GB.

The deltas come from kh-pcpatch with `--delta`, which turns KHPCPatchManager patches (`.kh1pcpatch`, `.kh2pcpatch`) into this format.

Tested on a Switch with version 1.0.0 (v0) of the game and the German voice patches for Kingdom Hearts Final Mix and Kingdom Hearts II Final Mix.

## How it works

The plugin hooks `nn::fs::OpenFile`, `CloseFile`, `GetFileSize` and the four `ReadFile` overloads in the SDK. When the game opens `rom:/<name>.pkg` and `rom:/<name>.pkg.delta` exists, reads past the end of the original archive are served from the delta and `GetFileSize` reports both sizes added up. All other files pass through unchanged. The plugin reads only from the game's own `rom:` mount, so it needs no SD card access. exlaunch itself needs system calls the game may not use (process memory mapping for the hooks), so each game gets its own `main.npdm` with those calls enabled.

## Build

Requires devkitPro with devkitA64 and libnx.

```sh
cd exlaunch
DEVKITPRO=/opt/devkitpro ./exlaunch.sh build
```

The plugin is `exlaunch/deploy/subsdk9`. Ignore the `main.npdm` next to it; use the patched game NPDM described below.

## Install

Each game in the collection is its own program:

| Program ID | Game |
| --- | --- |
| `010090C022978001` | Kingdom Hearts Final Mix |
| `010090C022978003` | Kingdom Hearts II Final Mix |

For every game you mod, the SD card needs:

```
atmosphere/contents/<program id>/
├── exefs/
│   ├── subsdk9                   # the plugin
│   └── main.npdm                 # the game's NPDM with SVCs 0x00-0x7F enabled
└── romfs/
    ├── <name>.hed                # from kh-pcpatch --delta
    └── <name>.pkg.delta          # from kh-pcpatch --delta
```

Create the hed and delta files from the game's original `.hed`/`.pkg` files:

```sh
kh-pcpatch.py apply kh2-romfs "German voices KH2 2.1.kh2pcpatch" -x '*/title.2ld/*' \
    --delta -o sd/atmosphere/contents/010090C022978003/romfs
kh-pcpatch.py verify sd/atmosphere/contents/010090C022978003/romfs --base kh2-romfs
cp exlaunch/deploy/subsdk9 sd/atmosphere/contents/010090C022978003/exefs/
tools/npdm-add-svcs.py kh2-exefs/main.npdm sd/atmosphere/contents/010090C022978003/exefs/main.npdm
```

`kh2-exefs/main.npdm` is the game's own NPDM from the program's ExeFS. The tool copies it and enables the system calls 0x00-0x7F in its ACI0 and ACID, keeping everything else. Atmosphère checks the signature of the original NPDM from the game and applies the permissions of the one on the SD card.

Delta files stay well below 4 GiB, so they need no special handling on FAT32.

After a game update that changes the archives, build new hed and delta files from the updated originals.

## License

GPL-2.0, like exlaunch, which is included in `exlaunch/` with these changes:

- `source/lib/init/crt0.s`: the module starts with `b +8`, which the rtld of SDK 21 and later requires for the classic module header
- `misc/specs/module.specs`: `--hash-style=both`
- `source/rtld.hpp`: `g_LookupGlobalManualFunctionPointer` is weak, because newer SDKs do not export it
- `source/lib/hook/nx64/hook_impl.cpp`: builds with current GCC
