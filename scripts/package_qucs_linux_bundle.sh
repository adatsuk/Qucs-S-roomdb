#!/usr/bin/env bash
# Package qucs-s + Qt/Cap'n Proto runtimes into a portable tar.gz (RHEL 8 / Rocky 8).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_PREFIX="${1:-$ROOT/dist-install}"
OUT_TAR="${2:-$ROOT/qucs-s-rocky8-bundle.tar.gz}"
BUNDLE_LABEL="${3:-Rocky Linux 8}"

QT_DIR="${QT_DIR:-/opt/qt/6.5.3/gcc_64}"
CAPNP_ROOT="${CAPNP_ROOT:-}"

BIN="$INSTALL_PREFIX/bin/qucs-s"
if [[ ! -x "$BIN" ]]; then
    echo "ERROR: qucs-s not found at $BIN (run cmake --install first)"
    exit 1
fi

DIST="$ROOT/dist-bundle"
STAGE="$ROOT/.bundle-stage"
rm -rf "$DIST" "$STAGE"
mkdir -p "$DIST/bin" "$DIST/lib" "$STAGE"

cp "$BIN" "$STAGE/qucs-s"
chmod +x "$STAGE/qucs-s"

if [[ -n "$CAPNP_ROOT" && -d "$CAPNP_ROOT/lib" ]]; then
    mkdir -p "$STAGE/lib"
    cp -a "$CAPNP_ROOT/lib"/lib*.so* "$STAGE/lib/" 2>/dev/null || true
fi

export PATH="${QT_DIR}/bin:${PATH}"
DEPLOY_LDPATH="$STAGE/lib"
if [[ -d "$QT_DIR/lib" ]]; then
    DEPLOY_LDPATH="${DEPLOY_LDPATH}:${QT_DIR}/lib"
fi
DEPLOY_LDPATH="${DEPLOY_LDPATH}:/usr/lib64:/lib64"
export LD_LIBRARY_PATH="$DEPLOY_LDPATH"

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/lib' "$STAGE/qucs-s" || true
fi

bundle_manual() {
    QMAKE="${QT_DIR}/bin/qmake" bash "$(dirname "$0")/bundle_qt_libs_manual.sh" "$STAGE/qucs-s" "$STAGE"
}

if [[ "${BUNDLE_SKIP_LINUXDEPLOYQT:-}" == "1" ]]; then
    bundle_manual
elif command -v linuxdeployqt >/dev/null 2>&1; then
    if ! linuxdeployqt "$STAGE/qucs-s" \
      -bundle-non-qt-libs -always-overwrite -no-translations \
      -no-plugins \
      -extra-plugins=platforms,imageformats,iconengines,styles \
      -qmake="${QT_DIR}/bin/qmake"; then
        echo "linuxdeployqt failed; falling back to manual Qt bundling." >&2
        bundle_manual
    fi
else
    echo "WARNING: linuxdeployqt not found; using manual Qt bundling."
    bundle_manual
fi

# Real ELF as qucs-s.bin; bin/qucs-s is a Qt-env wrapper (so ./bin/qucs-s never
# starts without platforms/xcb).
cp "$STAGE/qucs-s" "$DIST/bin/qucs-s.bin"
for so in "$STAGE"/*.so*; do
    [[ -e "$so" ]] || continue
    cp -a "$so" "$DIST/lib/"
done
if [[ -d "$STAGE/lib" ]]; then
    cp -a "$STAGE/lib/"* "$DIST/lib/" 2>/dev/null || true
fi
if [[ -d "$STAGE/plugins" ]]; then
    cp -a "$STAGE/plugins" "$DIST/"
fi

if [[ -d "$INSTALL_PREFIX/share" ]]; then
    cp -a "$INSTALL_PREFIX/share" "$DIST/"
fi

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/../lib' "$DIST/bin/qucs-s.bin" || true
fi

if [[ ! -f "$DIST/plugins/platforms/libqxcb.so" ]]; then
    echo "WARNING: plugins/platforms/libqxcb.so missing — GUI will fail on Linux." >&2
fi

# Shared launcher body: resolve bundle root, set Qt paths, exec ELF.
_write_qucs_launcher() {
    local out="$1" elf_rel="$2"
    cat >"$out" <<EOF
#!/usr/bin/env bash
set -euo pipefail
# Resolve symlinks so DIR is the real bundle even when invoked via <prefix>/bin/qucs-s.
_src="\${BASH_SOURCE[0]}"
while [[ -L "\$_src" ]]; do
  _dir="\$(cd -P "\$(dirname "\$_src")" && pwd)"
  _src="\$(readlink "\$_src")"
  [[ "\$_src" != /* ]] && _src="\$_dir/\$_src"
done
HERE="\$(cd -P "\$(dirname "\$_src")" && pwd)"
# bin/qucs-s → bundle root is parent; qucs-s-run.sh at root → HERE is bundle.
if [[ -x "\$HERE/bin/qucs-s.bin" ]]; then
  DIR="\$HERE"
elif [[ -x "\$HERE/qucs-s.bin" ]]; then
  DIR="\$(cd -P "\$HERE/.." && pwd)"
else
  DIR="\$HERE"
fi
export LD_LIBRARY_PATH="\$DIR/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="\$DIR/plugins\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}"
export QT_QPA_PLATFORM_PLUGIN_PATH="\$DIR/plugins/platforms\${QT_QPA_PLATFORM_PLUGIN_PATH:+:\$QT_QPA_PLATFORM_PLUGIN_PATH}"
export Qucs_sShareDir="\$DIR/share/qucs-s"
exec "\$DIR/${elf_rel}" "\$@"
EOF
    chmod +x "$out"
}

_write_qucs_launcher "$DIST/qucs-s-run.sh" "bin/qucs-s.bin"
_write_qucs_launcher "$DIST/bin/qucs-s" "bin/qucs-s.bin"

cat >"$DIST/BUNDLE.txt" <<EOF
Qucs-S + ROOM portable bundle ($BUNDLE_LABEL)
Built: $(date -u +%Y-%m-%dT%H:%M:%SZ)

Run: ./qucs-s-run.sh
  or: ./bin/qucs-s

Requires: Linux x86_64 with glibc 2.28+ (RHEL 8 / Rocky Linux 8 / AlmaLinux 8).
Host needs X11 xcb libs (libxcb, libxkbcommon-x11, …). Simulation backends
(ngspice, etc.) are not included — install separately if needed.
EOF

rm -rf "$STAGE"
tar -C "$ROOT" -czf "$OUT_TAR" "$(basename "$DIST")"
echo "Created $OUT_TAR"
