#!/usr/bin/env bash
set -euo pipefail

: "${VERSION:?VERSION must identify this build}"
: "${SDL_PREFIX:?SDL_PREFIX must locate the SDL3 installation}"

mkdir -p dist build/appimage-tools build/AppDir/usr/share/licenses/slipstream5000
cp LICENSE build/AppDir/usr/share/licenses/slipstream5000/LICENSE
cp src/opal/LICENSE build/AppDir/usr/share/licenses/slipstream5000/Opal-LICENSE
cp readme.md build/AppDir/usr/share/licenses/slipstream5000/readme.md

curl --fail --location --retry 3 \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
    --output build/appimage-tools/linuxdeploy.AppImage
chmod +x build/appimage-tools/linuxdeploy.AppImage
# Extract instead of requiring FUSE on the runner.
(cd build/appimage-tools && ./linuxdeploy.AppImage --appimage-extract >/dev/null)
export LD_LIBRARY_PATH="$SDL_PREFIX/lib:$SDL_PREFIX/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1 ARCH=x86_64
export LDAI_OUTPUT="slipstream5000-${VERSION}-linux-x86_64.AppImage"
build/appimage-tools/squashfs-root/AppRun \
    --appdir build/AppDir --executable build/slipstream5000 \
    --desktop-file Packaging/AppImage/slipstream5000.desktop \
    --icon-file Packaging/icons/slipstream5000.png --output appimage
mv "$LDAI_OUTPUT" dist/
test -s "dist/$LDAI_OUTPUT"
# Verify that SDL3 was actually bundled.
find build/AppDir -name 'libSDL3.so*' -print -quit | grep -q .
