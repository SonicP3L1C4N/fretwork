#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#
# Builds the AppImage: a release build, everything it links against, the Qt
# plugins and QML it loads at runtime, and a General MIDI soundfont, in one
# file that runs without being installed.
#
# What this does NOT bundle, deliberately:
#
#   glibc, libstdc++, and the graphics stack. A bundle carrying its own libc
#   is a bundle that crashes on the first machine whose kernel disagrees with
#   it, and one carrying its own libGL cannot talk to the driver that is
#   actually there. The consequence is a floor rather than a crash: this
#   AppImage runs on distributions whose glibc is at least as new as the one
#   it was built against, which is printed at the end so it can be written
#   down rather than guessed at.
#
#   LV2 plugins. The amplifier simulations this program is worth using with
#   run to hundreds of megabytes, and a guitarix the user installed is a
#   guitarix that gets security updates. The AppImage finds whatever is on
#   the host, and says so in the effects menu when there is nothing.
#
# Usage: packaging/appimage/build.sh [output-directory]

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${1:-$root/dist}"
build="$root/build-appimage"
appdir="$root/AppDir"
tools="${APPIMAGE_TOOLS:-$root/.appimage-tools}"

# The soundfont to carry. FluidR3 is MIT, which is why it can be carried at
# all; a bundle is redistribution, and most of the good banks are not.
soundfont="${FRETWORK_BUNDLE_SOUNDFONT:-/usr/share/sounds/sf2/FluidR3_GM.sf2}"

say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }

# ---- the tools, fetched once ----

say "Build tools"
mkdir -p "$tools"
fetch() {
    local name="$1" url="$2"
    if [ ! -x "$tools/$name" ]; then
        echo "fetching $name"
        curl -sSfL -o "$tools/$name" "$url"
        chmod +x "$tools/$name"
    else
        echo "have $name"
    fi
}
fetch linuxdeploy.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fetch appimagetool.AppImage \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage

# ---- the program ----

say "Building"
cmake -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF >/dev/null
cmake --build "$build" -j"$(nproc)"

rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" >/dev/null

version="$("$build/bin/fretwork" --version 2>/dev/null | awk '{print $2}')"
[ -n "$version" ] || { echo "could not read a version out of the binary" >&2; exit 1; }
echo "fretwork $version"

# ---- everything it needs at runtime ----

say "Bundling"

# qmlimportscanner lives in libexec and is not on anybody's PATH.
qtlibexec="$(qmake6 -query QT_INSTALL_LIBEXECS 2>/dev/null || echo /usr/lib/qt6/libexec)"
export PATH="$qtlibexec:$PATH"
export QMAKE="${QMAKE:-/usr/bin/qmake6}"

# The QML this program imports is scanned out of its sources; the modules it
# imports in turn come with them.
export QML_SOURCES_PATHS="$root/src/gui"

# Platform plugins are chosen at runtime by the session, not at build time, so
# guessing wrong means a window that never opens. Wayland because that is what
# a current Plasma session is; xcb comes by default; offscreen because
# --offscreen is a documented option of this program and it is the plugin that
# option needs.
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so;libqminimal.so"

"$tools/linuxdeploy.AppImage" --appdir "$appdir" \
    --desktop-file "$appdir/usr/share/applications/io.github.sonicp3l1c4n.fretwork.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/io.github.sonicp3l1c4n.fretwork.svg" \
    --executable "$appdir/usr/bin/fretwork" \
    --plugin qt

# ---- what the scanner cannot see ----
#
# qmlimportscanner reads import statements, and nothing below is ever imported
# by anything this program's sources say: the Controls style is chosen at
# runtime by Kirigami, the style imports a spell checker's settings object, and
# the icon theme is asked for by name. All of it is therefore absent from a
# bundle assembled only from what the sources ask for -- which is a window that
# does not open at all, and, once it does, a window with no icons in it. Each
# one of these was found by the window failing to open and saying which.

say "Style, plugins and icons"
qtqml="$(qmake6 -query QT_INSTALL_QML 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/qml)"
qtplugins="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)"

# The plugin the Wayland platform plugin loads to get a buffer from the
# graphics driver. Without it the program aborts on a Wayland session --
# "Available client buffer integrations: QList()", and no window -- while the
# same bundle run offscreen or on X11 is perfectly happy, which is why this
# was found by running it on a real desktop and not by any test.
# And the icon engine that draws an SVG. Breeze is SVG from top to bottom, so
# without this every themed icon in the program is found, loaded, and drawn as
# nothing: a toolbar of blank buttons that all still work. The SVG *image*
# format plugin is deployed automatically and is not the same thing.
for plugin in wayland-graphics-integration-client iconengines; do
    if [ -d "$qtplugins/$plugin" ]; then
        cp -r "$qtplugins/$plugin" "$appdir/usr/plugins/"
        echo "plugins $plugin"
    else
        echo "no $qtplugins/$plugin on this machine" >&2
    fi
done
for module in org/kde/desktop org/kde/qqc2desktopstyle org/kde/sonnet; do
    if [ -d "$qtqml/$module" ]; then
        install -d "$appdir/usr/qml/$(dirname "$module")"
        cp -r "$qtqml/$module" "$appdir/usr/qml/$module"
        echo "qml $module"
    else
        echo "no $module on this machine -- the window will not open" >&2
    fi
done

# Breeze, because a KDE program asks for icons by Breeze's names and gets
# nothing anywhere else. Light only: the dark theme is another forty megabytes
# and Breeze's symbolic icons are recoloured to the palette in use anyway.
if [ -d /usr/share/icons/breeze ]; then
    install -d "$appdir/usr/share/icons"
    cp -r /usr/share/icons/breeze "$appdir/usr/share/icons/breeze"
    echo "icons breeze"
fi

# Now that those are in place, let linuxdeploy work out what they link
# against and point them at it.
for extra in usr/qml/org/kde/desktop usr/qml/org/kde/qqc2desktopstyle \
             usr/qml/org/kde/sonnet usr/plugins/wayland-graphics-integration-client \
             usr/plugins/iconengines; do
    [ -d "$appdir/$extra" ] || continue
    "$tools/linuxdeploy.AppImage" --appdir "$appdir" \
        --deploy-deps-only="$appdir/$extra"
done

# ---- the soundfont ----

if [ -f "$soundfont" ]; then
    say "Soundfont"
    install -d "$appdir/usr/share/sounds/sf2"
    cp "$soundfont" "$appdir/usr/share/sounds/sf2/FluidR3_GM.sf2"
    install -d "$appdir/usr/share/doc/fretwork"
    for licence in /usr/share/doc/fluid-soundfont-gm/copyright \
                   /usr/share/licenses/soundfont-fluid/LICENSE; do
        [ -f "$licence" ] && cp "$licence" "$appdir/usr/share/doc/fretwork/FluidR3_GM.licence" && break
    done
    du -h "$appdir/usr/share/sounds/sf2/FluidR3_GM.sf2" | cut -f1
else
    echo "no soundfont at $soundfont -- the AppImage will need one on the host" >&2
fi

# ---- what the bundle says for itself before starting ----

say "AppRun"
rm -f "$appdir/AppRun"
cp "$here/AppRun" "$appdir/AppRun"
chmod +x "$appdir/AppRun"

# ---- what has to be there ----
#
# Every line of this list was once missing, and the way each announced itself
# was a window that would not open on somebody else's machine. A bundle is
# assembled by tools that fail quietly -- linuxdeploy ignored EXTRA_QT_PLUGINS
# without a word -- so the assembly is checked rather than trusted.

say "Checking"
missing=0
need() {
    if [ -e "$appdir/$1" ]; then
        echo "  ok   $1"
    else
        echo "  MISSING  $1  -- $2" >&2
        missing=1
    fi
}
need usr/bin/fretwork "the program"
need usr/bin/qt.conf "how Qt finds the rest of this"
need usr/plugins/platforms/libqwayland.so "no window on a Wayland session"
need usr/plugins/platforms/libqxcb.so "no window on X11"
need usr/plugins/platforms/libqoffscreen.so "--offscreen would not work"
need usr/plugins/wayland-graphics-integration-client "Wayland aborts with no buffer integration"
need usr/qml/org/kde/kirigami "the interface is Kirigami"
need usr/qml/org/kde/desktop "the Controls style Kirigami picks"
need usr/qml/org/kde/sonnet "the style imports it, for spelling settings"
need usr/plugins/iconengines/libqsvgicon.so "Breeze is SVG; without this every icon is blank"
need usr/share/icons/breeze "a window with no icons in it"
[ "$missing" -eq 0 ] || { echo "bundle is incomplete" >&2; exit 1; }

# ---- one file ----

say "Packaging"
mkdir -p "$out"
target="$out/Fretwork-$version-x86_64.AppImage"
rm -f "$target"
ARCH=x86_64 "$tools/appimagetool.AppImage" "$appdir" "$target"

say "Done"
ls -lh "$target" | awk '{print $5, $9}'
( cd "$out" && sha256sum "$(basename "$target")" > "$(basename "$target").sha256" )

# The floor this build imposes, measured rather than assumed: the highest
# glibc symbol version anything in the bundle asks for.
floor="$(find "$appdir" -type f \( -name '*.so*' -o -perm -u+x \) -print0 2>/dev/null \
    | xargs -0 -r objdump -T 2>/dev/null \
    | grep -oE 'GLIBC_2\.[0-9]+' | sort -u -t. -k2 -n | tail -1)"
echo "needs ${floor:-an unknown glibc} or newer"
