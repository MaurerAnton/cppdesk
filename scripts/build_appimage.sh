#!/bin/bash
# Build AppImage for cppdesk
set -e

APP=cppdesk
VERSION=1.3.0
BUILD_DIR=build-appimage
APPDIR=${BUILD_DIR}/${APP}.AppDir

# Build the application
cmake -B ${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build ${BUILD_DIR} --parallel $(nproc)
DESTDIR=${APPDIR} cmake --install ${BUILD_DIR}

# Copy desktop file and icon
mkdir -p ${APPDIR}/usr/share/applications
mkdir -p ${APPDIR}/usr/share/icons/hicolor/256x256/apps
cp flatpak/com.cppdesk.CppDesk.desktop ${APPDIR}/usr/share/applications/
cp res/cppdesk.png ${APPDIR}/usr/share/icons/hicolor/256x256/apps/

# Create AppRun
cat > ${APPDIR}/AppRun << 'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export GTK_PATH="${HERE}/usr/lib/gtk-3.0"
exec "${HERE}/usr/bin/cppdesk" "$@"
APPRUN
chmod +x ${APPDIR}/AppRun

# Download appimagetool if needed
if [ ! -f appimagetool ]; then
    wget -q https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage -O appimagetool
    chmod +x appimagetool
fi

# Create AppImage
ARCH=x86_64 ./appimagetool ${APPDIR} ${APP}-${VERSION}-x86_64.AppImage
echo "AppImage created: ${APP}-${VERSION}-x86_64.AppImage"
