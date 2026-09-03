# SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

cmake_minimum_required(VERSION 3.20)

project(fretwork VERSION 0.4.1)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(ECMInstallIcons)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMDeprecationSettings)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core Gui Widgets Xml Qml Quick QuickControls2 Test)

# zlib reads the deflate streams inside a .gp. KArchive is here only so the
# tests can *write* archives to read back; the reading is ours, for the reason
# set out in src/import/zipreader.h.
find_package(ZLIB REQUIRED)

# FluidSynth, one instance per track: the design's whole point, and why its
# absence is an error rather than a lost feature.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FluidSynth REQUIRED IMPORTED_TARGET fluidsynth)

# Only to call pw_init(). FluidSynth's PipeWire driver expects the host
# application to have initialised the library, and says so at the point where
# it cannot find a single SPA plugin.
# lilv finds and loads LV2 plugins: the per-track amplifier simulation that
# makes stems worth having, hosted in this process rather than delegated --
# see the architecture note on why.
pkg_check_modules(Lilv IMPORTED_TARGET lilv-0)

pkg_check_modules(PipeWire IMPORTED_TARGET libpipewire-0.3)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Archive CoreAddons I18n Kirigami)

ecm_set_disabled_deprecation_versions(QT 6.6 KF 6.0)

ecm_install_icons(ICONS
    icons/24-apps-io.github.sonicp3l1c4n.fretwork.png
    icons/32-apps-io.github.sonicp3l1c4n.fretwork.png
    icons/48-apps-io.github.sonicp3l1c4n.fretwork.png
    icons/64-apps-io.github.sonicp3l1c4n.fretwork.png
    icons/96-apps-io.github.sonicp3l1c4n.fretwork.png
    icons/128-apps-io.github.sonicp3l1c4n.fretwork.png
    icons/sc-apps-io.github.sonicp3l1c4n.fretwork.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

# ECM has no naming convention for symbolic icons, which live in a directory of
# their own rather than a size-named one.
install(FILES icons/io.github.sonicp3l1c4n.fretwork-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/symbolic/apps)

# The manual page. Configured rather than installed as it stands, so that the
# version in its header comes from the one place a version is written down --
# the AppStream metainfo already has to be kept in step by hand, and a third
# copy to remember is a third copy that will one day be wrong.
configure_file(docs/fretwork.1.in ${CMAKE_CURRENT_BINARY_DIR}/fretwork.1 @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/fretwork.1
        DESTINATION ${KDE_INSTALL_MANDIR}/man1)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
