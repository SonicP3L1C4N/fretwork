# SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

cmake_minimum_required(VERSION 3.20)

project(fretwork VERSION 0.1.0)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
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
pkg_check_modules(PipeWire IMPORTED_TARGET libpipewire-0.3)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Archive CoreAddons I18n Kirigami)

ecm_set_disabled_deprecation_versions(QT 6.6 KF 6.0)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
