# ROOM integration in Qucs-S

Qucs-S can open and save schematic views in the [ROOM](https://github.com/IHP-GmbH/Room) format (`.room`, `*.schematic.room`) when built with `QUCS_ENABLE_ROOM=ON` (default).

## Layout

| Path | Role |
|------|------|
| `../CommonDB` | ROOM library (sibling checkout) |
| `../LibMan/capnp-install` | Cap'n Proto prefix (reused automatically) |
| `qucs/room_schematic_io.*` | ROOM ↔ Qucs `.sch` bridge |
| `cmake/FetchRoom.cmake` | CMake subproject for ROOM |

## Build (Windows, Qt 6)

```powershell
cd C:\Users\anton\Documents\Qucs-S-roomdb
git submodule update --init --depth 1

mkdir build
cd build
cmake .. -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/mingw_64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DQUCS_ENABLE_ROOM=ON `
  -DQUCS_ROOM_SOURCE_DIR=C:/Users/anton/Documents/CommonDB `
  -DCAPNP_ROOT=C:/Users/anton/Documents/LibMan/capnp-install

cmake --build . --target qucs-s -j
```

Requires: Qt 6, CMake, flex, bison, gperf, MinGW (same toolchain as Qt).

## Usage

- **File → Open** — select `*.room` or `*.schematic.room`
- **File → Save** — writes back to ROOM when the document path is a `.room` file
- Round-trip uses CommonDB `QucsImporter` / `QucsExporter` internally

## Disable ROOM

```powershell
cmake .. -DQUCS_ENABLE_ROOM=OFF
```
