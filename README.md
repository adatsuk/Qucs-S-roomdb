# Qucs-S-coredb

[Qucs-S](https://github.com/ra3xdh/qucs_s) fork with [CORE](https://github.com/IHP-GmbH/CommonDB) (CommonDB) integration: open and save `.core` / `*.schematic.core` schematics, round-trip through CommonDB `QucsImporter` / `QucsExporter`.

Based on upstream Qucs-S with CORE patches in `qucs/core_schematic_io.*` and `cmake/FetchCore.cmake`.

## Layout

| Path | Purpose |
|------|---------|
| `../CommonDB` | CORE library (sibling checkout, preferred at configure time) |
| `../LibMan/capnp-install` | Cap'n Proto prefix on Windows (auto-detected) |
| `qucs/core_schematic_io.*` | CORE ↔ Qucs `.sch` bridge |
| `cmake/FetchCore.cmake` | CMake subproject for CORE |
| `docs/CORE.md` | Build notes (Windows / CMake flags) |
| `run-qucs-s.bat` | Launch locally built `qucs-s.exe` (Windows) |

## Prerequisites

- **Qt 6** (Core, Gui, Widgets, Svg, Xml, PrintSupport)
- **CMake** ≥ 3.10, **Ninja** or Make, **flex**, **bison**, **gperf**
- **CommonDB** as a sibling directory, e.g. `../CommonDB`
- Cap'n Proto: reuse `../LibMan/capnp-install` (Windows) or bootstrap via CommonDB on Linux

## Quick start (Linux)

```bash
git clone https://github.com/adatsuk/Qucs-S-coredb.git
git clone https://github.com/IHP-GmbH/CommonDB.git
cd Qucs-S-coredb
git submodule update --init --depth 1   # qucsator_rf, qucs-s-spar-viewer, rxcalc

mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DQUCS_CORE_SOURCE_DIR=../CommonDB
cmake --build . --target qucs-s -j"$(nproc)"
```

Set `CAPNP_ROOT` if Cap'n Proto is not found automatically (see `cmake/EnsureCapnp.cmake`).

## Coordinate scale

CORE schematic views use integer DBU with `dbuPerEditorUnit` (Qucs = 1, Xschem = 1000). See [CommonDB coord scale docs](https://github.com/IHP-GmbH/CommonDB/blob/main/docs/html/coordscale.html).

## CI

GitHub Actions (`.github/workflows/ci.yaml`) builds **qucs-s** with CORE on **Rocky Linux 8** (RHEL 8 compatible): checks out CommonDB, bootstraps Cap'n Proto (cached), installs Qt 6, runs CMake + Ninja.

## Upstream

This tree tracks [ra3xdh/qucs_s](https://github.com/ra3xdh/qucs_s) `current` (26.1.1+) with local CORE changes. For stock Qucs-S releases without CORE, use upstream.

## License

GPL-2.0 (same as upstream Qucs-S). See upstream and CommonDB for details.
