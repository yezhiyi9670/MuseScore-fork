#!/usr/bin/env bash

# Build fcitx5-qt's Qt5 platform input context plugin against the very same
# Qt that the AppImage bundles, and stage it for make_appimage.sh.
#
# Why this is needed: Qt only loads a platform input context plugin when the
# Qt minor version the plugin was built with is not newer than the running Qt
# (see QLibraryPrivate::isPlugin() in qtbase). The AppImage bundles Qt 5.9.8,
# so the plugin shipped by the distro (built against Qt 5.15) is rejected.
# Bundling a plugin built against 5.9.8 makes "QT_IM_MODULE=fcitx" work on any
# machine, independent of what the host distribution has installed.

echo "Build fcitx5-qt input method plugin"
trap 'echo fcitx5-qt plugin build failed; exit 1' ERR

# setup.sh cds four levels up from build/ci/linux (i.e. to the parent of the
# repository) and writes musescore_environment.sh there; the other CI scripts
# pick it up as "./../musescore_environment.sh" from the repository root.
SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "${0}")")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
ENV_FILE="${REPO_DIR}/../musescore_environment.sh"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "error: ${ENV_FILE} not found, run build/ci/linux/setup.sh first" >&2
  exit 1
fi

# shellcheck source=/dev/null
source "${ENV_FILE}"

FCITX5_QT_TAG="5.1.16" # same minor series as the fcitx5 daemon on modern distros
FCITX5_QT_SRC="/tmp/fcitx5-qt-src"
FCITX5_QT_BUILD="/tmp/fcitx5-qt-build"
FCITX5_QT_STAGE="${REPO_DIR}/build.artifacts/fcitx5-qt"

df -h .

if [[ ! -f "${QT_PATH}/lib/libQt5DBus.so.5" ]]; then
  echo "error: ${QT_PATH} has no QtDBus; cannot build the fcitx5-qt plugin" >&2
  exit 1
fi

echo "QT_PATH=${QT_PATH}"
"${QT_PATH}/bin/qmake" -v 2>/dev/null || true

apt-get install -y --no-install-recommends \
  extra-cmake-modules \
  libxkbcommon-dev \
  libxcb1-dev \
  libx11-dev

rm -rf "${FCITX5_QT_SRC}" "${FCITX5_QT_BUILD}" "${FCITX5_QT_STAGE}"

git clone --depth 1 --branch "${FCITX5_QT_TAG}" \
  https://github.com/fcitx/fcitx5-qt "${FCITX5_QT_SRC}"

# The Qt5 code path never includes Qt private headers (the single user of
# QtGui private headers is a Qt6-only Wayland workaround), so drop the
# "Private" component. That keeps the build working even if the bundled Qt SDK
# does not ship private headers.
sed -i \
  's/find_package(Qt5Gui ${REQUIRED_QT5_VERSION} REQUIRED Private)/find_package(Qt5Gui ${REQUIRED_QT5_VERSION} REQUIRED)/' \
  "${FCITX5_QT_SRC}/qt5/CMakeLists.txt"

# BUILD_ONLY_PLUGIN turns Fcitx5Qt5DBusAddons into an OBJECT library that is
# linked into the plugin, so a single .so is produced and no fcitx5 development
# package is required.
cmake -S "${FCITX5_QT_SRC}" -B "${FCITX5_QT_BUILD}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${QT_PATH}" \
  -DCMAKE_INSTALL_PREFIX="${FCITX5_QT_STAGE}" \
  -DCMAKE_INSTALL_QT5PLUGINDIR="${FCITX5_QT_STAGE}/plugins" \
  -DCMAKE_INSTALL_RPATH='$ORIGIN/../../lib;$ORIGIN/../lib;$ORIGIN' \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DBUILD_ONLY_PLUGIN=ON \
  -DENABLE_QT5=ON \
  -DENABLE_QT6=OFF \
  -DENABLE_QT4=OFF

cmake --build "${FCITX5_QT_BUILD}" -j"$(nproc)"
cmake --install "${FCITX5_QT_BUILD}"

plugin="${FCITX5_QT_STAGE}/plugins/platforminputcontexts/libfcitx5platforminputcontextplugin.so"
if [[ ! -f "${plugin}" ]]; then
  echo "error: ${plugin} was not produced" >&2
  exit 1
fi

echo "Built ${plugin}"
ldd "${plugin}" || true

# Copy into the artifacts directory as well, so a workflow run can be used to
# inspect the .so before the packaging step is wired up.
mkdir -p "${REPO_DIR}/build.artifacts/fcitx5-qt"
cp -L "${plugin}" "${REPO_DIR}/build.artifacts/fcitx5-qt/"

df -h .
echo "fcitx5-qt plugin build done"
