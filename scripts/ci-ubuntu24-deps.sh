#!/usr/bin/env bash
# Install build dependencies on Ubuntu 24.04 (CI and local).
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.5.3}"
QT_INSTALL_ROOT="${QT_INSTALL_ROOT:-/opt/qt}"
QT_DIR="${QT_INSTALL_ROOT}/${QT_VERSION}/gcc_64"

export DEBIAN_FRONTEND=noninteractive
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build git pkg-config curl patchelf \
  flex bison gperf dos2unix \
  python3 python3-pip python3-venv \
  autoconf automake libtool zlib1g-dev \
  libx11-dev libxcb1-dev libgl1-mesa-dev \
  libfreetype6-dev libfontconfig1-dev \
  libxkbcommon-dev libxkbcommon-x11-dev \
  libxcb-util0-dev libxcb-image0-dev libxcb-keysyms1-dev \
  libxcb-render-util0-dev libxcb-icccm4-dev libxcb-cursor-dev \
  xvfb

if [[ ! -x "${QT_DIR}/bin/qmake" ]]; then
  # Do not upgrade distro pip on Ubuntu 24+ (deb-installed pip has no RECORD).
  python3 -m pip install --break-system-packages 'aqtinstall<4'
  python3 -m aqt install-qt linux desktop "${QT_VERSION}" gcc_64 \
    -O "${QT_INSTALL_ROOT}"
fi

if [[ ! -x "${QT_DIR}/bin/lrelease" ]]; then
  QTTOOLS_SRC="${QTTOOLS_SRC:-/tmp/qttools-${QT_VERSION}}"
  if [[ ! -d "${QTTOOLS_SRC}/.git" ]]; then
    git clone --depth 1 --branch "v${QT_VERSION}" \
      https://code.qt.io/qt/qttools.git "${QTTOOLS_SRC}"
  fi
  cmake -S "${QTTOOLS_SRC}" -B "${QTTOOLS_SRC}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_DIR}" \
    -DCMAKE_INSTALL_PREFIX="${QT_DIR}" \
    -DFEATURE_assistant=OFF \
    -DFEATURE_designer=OFF \
    -DFEATURE_linguist=ON \
    -DFEATURE_qdoc=OFF \
    -DFEATURE_qtattributionsscanner=OFF \
    -DFEATURE_pixeltool=OFF \
    -DFEATURE_distancefieldgenerator=OFF
  cmake --build "${QTTOOLS_SRC}/build" --target install -j"$(nproc)"
fi

export PATH="${QT_DIR}/bin:${PATH}"
export CMAKE_PREFIX_PATH="${QT_DIR}"
export LD_LIBRARY_PATH="${QT_DIR}/lib:${LD_LIBRARY_PATH:-}"

if [[ -n "${GITHUB_ENV:-}" ]]; then
  {
    echo "PATH=${PATH}"
    echo "CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    echo "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}"
    echo "QT_DIR=${QT_DIR}"
  } >> "${GITHUB_ENV}"
fi

echo "GCC: $(gcc --version | head -1)"
echo "Qt: $("${QT_DIR}/bin/qmake" -query QT_VERSION 2>/dev/null || echo "${QT_VERSION}")"
