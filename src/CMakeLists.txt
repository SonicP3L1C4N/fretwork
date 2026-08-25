# SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

# The program without its command line: everything the tests exercise, and
# everything a future window would link against.
add_library(fretworkcore STATIC
    import/gpif.cpp
    import/zipreader.cpp
    export/midi.cpp
    audio/renderer.cpp
    audio/tracksynth.cpp
    audio/player.cpp
    audio/wav.cpp
    render/tablayout.cpp
    render/tabpainter.cpp
    playback/timeline.cpp
)

target_include_directories(fretworkcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/model
    ${CMAKE_CURRENT_SOURCE_DIR}/import
    ${CMAKE_CURRENT_SOURCE_DIR}/playback
    ${CMAKE_CURRENT_SOURCE_DIR}/export
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/render
)

# ZLIB rather than KF6::Archive: see src/import/zipreader.h for why this
# project reads its own ZIPs, which is not a decision taken lightly.
target_link_libraries(fretworkcore PUBLIC Qt6::Core Qt6::Gui Qt6::Xml ZLIB::ZLIB
                                         PkgConfig::FluidSynth)

if(PipeWire_FOUND)
    target_link_libraries(fretworkcore PRIVATE PkgConfig::PipeWire)
    target_compile_definitions(fretworkcore PRIVATE FRETWORK_HAVE_PIPEWIRE)
endif()

add_executable(fretwork main.cpp)
target_link_libraries(fretwork PRIVATE fretworkcore Qt6::Gui KF6::CoreAddons KF6::I18n)
target_compile_definitions(fretwork PRIVATE FRETWORK_VERSION="${PROJECT_VERSION}")

install(TARGETS fretwork ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
