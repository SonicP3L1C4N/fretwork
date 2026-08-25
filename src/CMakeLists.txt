# SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

# The program without its command line: everything the tests exercise, and
# everything a future window would link against.
add_library(fretworkcore STATIC
    import/gpif.cpp
    import/zipreader.cpp
    export/midi.cpp
    audio/renderer.cpp
    audio/wav.cpp
    playback/timeline.cpp
)

target_include_directories(fretworkcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/model
    ${CMAKE_CURRENT_SOURCE_DIR}/import
    ${CMAKE_CURRENT_SOURCE_DIR}/playback
    ${CMAKE_CURRENT_SOURCE_DIR}/export
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
)

# ZLIB rather than KF6::Archive: see src/import/zipreader.h for why this
# project reads its own ZIPs, which is not a decision taken lightly.
target_link_libraries(fretworkcore PUBLIC Qt6::Core Qt6::Xml ZLIB::ZLIB
                                         PkgConfig::FluidSynth)

add_executable(fretwork main.cpp)
target_link_libraries(fretwork PRIVATE fretworkcore KF6::CoreAddons KF6::I18n)
target_compile_definitions(fretwork PRIVATE FRETWORK_VERSION="${PROJECT_VERSION}")

install(TARGETS fretwork ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
