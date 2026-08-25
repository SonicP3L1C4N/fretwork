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
    format/fwformat.cpp
    edit/cursor.cpp
    edit/editor.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/edit
    ${CMAKE_CURRENT_SOURCE_DIR}/format
)

# ZLIB rather than KF6::Archive: see src/import/zipreader.h for why this
# project reads its own ZIPs, which is not a decision taken lightly.
target_link_libraries(fretworkcore PUBLIC Qt6::Core Qt6::Gui Qt6::Xml ZLIB::ZLIB
                                         PkgConfig::FluidSynth
                                  PRIVATE KF6::I18n KF6::Archive)

if(PipeWire_FOUND)
    target_link_libraries(fretworkcore PRIVATE PkgConfig::PipeWire)
    target_compile_definitions(fretworkcore PRIVATE FRETWORK_HAVE_PIPEWIRE)
endif()

# The window. The QML types are registered by the target that owns the QML
# module, so the session and the view live here rather than in the library --
# and nothing in the library knows that QML exists, which is what keeps the
# model, the timeline and the audio testable without one.
add_executable(fretwork
    main.cpp
    gui/session.cpp
    gui/scoreview.cpp
)

# The generated type registrations include these headers by their own names.
target_include_directories(fretwork PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)

# The application icon, compiled in: an installed copy takes it from the icon
# theme, and one run out of the build directory has no theme to take it from.
qt_add_resources(fretwork "appicon"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}/icons
    FILES ${CMAKE_SOURCE_DIR}/icons/sc-apps-io.github.sonicp3l1c4n.fretwork.svg
)

# Aliased to the root of the module: without this the type is registered under
# the directory it happens to sit in, and the module has no "Main" in it.
set_source_files_properties(gui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)

qt_add_qml_module(fretwork
    URI org.kde.fretwork
    VERSION 1.0
    QML_FILES
        gui/Main.qml
)

target_link_libraries(fretwork PRIVATE
    fretworkcore
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::Kirigami
)
target_compile_definitions(fretwork PRIVATE FRETWORK_VERSION="${PROJECT_VERSION}")

install(TARGETS fretwork ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS io.github.sonicp3l1c4n.fretwork.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
