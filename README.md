# Qucs-S-roomdb

[Qucs-S](https://github.com/ra3xdh/qucs_s) fork with [ROOM](https://github.com/IHP-GmbH/Room) (CommonDB) integration: open and save `.room` / `*.schematic.room` schematics, round-trip through CommonDB `QucsImporter` / `QucsExporter`.

Based on upstream Qucs-S with ROOM patches in `qucs/room_schematic_io.*` and `cmake/FetchRoom.cmake`.

## Layout

| Path | Purpose |
|------|---------|
| `../CommonDB` | ROOM library (sibling checkout, preferred at configure time) |
| `../LibMan/capnp-install` | Cap'n Proto prefix on Windows (auto-detected) |
| `qucs/room_schematic_io.*` | ROOM ↔ Qucs `.sch` bridge |
| `cmake/FetchRoom.cmake` | CMake subproject for ROOM |
| `docs/ROOM.md` | Build notes (Windows / CMake flags) |
| `run-qucs-s.bat` | Launch locally built `qucs-s.exe` (Windows) |

## Prerequisites

- **Qt 6** (Core, Gui, Widgets, Svg, Xml, PrintSupport)
- **CMake** ≥ 3.10, **Ninja** or Make, **flex**, **bison**, **gperf**
- **CommonDB** as a sibling directory, e.g. `../CommonDB`
- Cap'n Proto: reuse `../LibMan/capnp-install` (Windows) or bootstrap via CommonDB on Linux

## Quick start (Linux)

```bash
git clone https://github.com/adatsuk/Qucs-S-roomdb.git
git clone https://github.com/IHP-GmbH/Room.git
cd Qucs-S-roomdb
git submodule update --init --depth 1   # qucsator_rf, qucs-s-spar-viewer, rxcalc

mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DQUCS_ROOM_SOURCE_DIR=../CommonDB
cmake --build . --target qucs-s -j"$(nproc)"
```

Set `CAPNP_ROOT` if Cap'n Proto is not found automatically (see `cmake/EnsureCapnp.cmake`).

## Coordinate scale

ROOM schematic views use integer DBU with `dbuPerEditorUnit` (Qucs = 1, Xschem = 1000). See [CommonDB coord scale docs](https://github.com/IHP-GmbH/Room/blob/main/docs/html/coordscale.html).

## CI

GitHub Actions (`.github/workflows/ci.yaml`) builds **qucs-s** with ROOM on **Rocky Linux 8** (RHEL 8 compatible): checks out CommonDB, bootstraps Cap'n Proto (cached), installs Qt 6, runs CMake + Ninja.

## Upstream

This tree tracks [ra3xdh/qucs_s](https://github.com/ra3xdh/qucs_s) `current` (26.1.1+) with local ROOM changes. For stock Qucs-S releases without ROOM, use upstream.

## License

GPL-2.0 (same as upstream Qucs-S). See upstream and CommonDB for details.
