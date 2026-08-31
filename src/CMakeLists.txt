# SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

# The program without its command line: everything the tests exercise, and
# everything a future window would link against.
add_library(fretworkcore STATIC
    model/notevalue.cpp
    model/instruments.cpp
    model/notename.cpp
    model/fretboard.cpp
    model/key.cpp
    model/analysis.cpp
    model/chord.cpp
    import/gpif.cpp
    import/zipreader.cpp
    export/midi.cpp
    audio/renderer.cpp
    audio/tracksynth.cpp
    audio/player.cpp
    audio/audioinput.cpp
    audio/jacktransport.cpp
    audio/lv2chain.cpp
    audio/pitchdetector.cpp
    audio/portedoutput.cpp
    audio/sampler.cpp
    audio/gxpreset.cpp
    audio/sfz.cpp
    audio/tuner.cpp
    audio/wav.cpp
    format/fwformat.cpp
    format/rigfile.cpp
    edit/cursor.cpp
    edit/editor.cpp
    render/tablayout.cpp
    render/tabpainter.cpp
    playback/swing.cpp
    playback/timeline.cpp
)

# The version, so that a saved file can say which build wrote it. The window
# and the command line already had this; the format needs it too.
target_compile_definitions(fretworkcore PRIVATE FRETWORK_VERSION="${PROJECT_VERSION}")

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

if(Lilv_FOUND)
    target_link_libraries(fretworkcore PRIVATE PkgConfig::Lilv)
    target_compile_definitions(fretworkcore PRIVATE FRETWORK_HAVE_LILV)
endif()

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
    gui/tuning.cpp
)

# The generated type registrations include these headers by their own names.
target_include_directories(fretwork PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)

# The application icon, compiled in: an installed copy takes it from the icon
# theme, and one run out of the build directory has no theme to take it from.
qt_add_resources(fretwork "appicon"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}/icons
    FILES ${CMAKE_SOURCE_DIR}/icons/sc-apps-io.github.sonicp3l1c4n.fretwork.svg
          ${CMAKE_SOURCE_DIR}/icons/instrument-guitar.svg
          ${CMAKE_SOURCE_DIR}/icons/instrument-bass.svg
          ${CMAKE_SOURCE_DIR}/icons/instrument-drums.svg
          ${CMAKE_SOURCE_DIR}/icons/instrument-keys.svg
          ${CMAKE_SOURCE_DIR}/icons/instrument-note.svg
          # The same badges in paper, for the panels that are drawn on ink: a
          # disc the colour of the chrome is a hole in it.
          ${CMAKE_SOURCE_DIR}/icons/instrument-guitar-paper.svg
          ${CMAKE_SOURCE_DIR}/icons/instrument-bass-paper.svg
          ${CMAKE_SOURCE_DIR}/icons/instrument-drums-paper.svg
          ${CMAKE_SOURCE_DIR}/icons/instrument-keys-paper.svg
          ${CMAKE_SOURCE_DIR}/icons/instrument-note-paper.svg
)

# The chrome's typeface, compiled in for the same reason the icon is: a font
# the program depends on and does not carry is a font that is missing on
# somebody else's machine, and the window would fall back to a sans without
# saying so. Vendored unmodified under the SIL Open Font License, whose terms
# travel with it in fonts/LICENSE.md.
qt_add_resources(fretwork "chromefont"
    PREFIX "/fonts"
    BASE ${CMAKE_SOURCE_DIR}/fonts
    FILES ${CMAKE_SOURCE_DIR}/fonts/SourceSerif4-Regular.ttf
          ${CMAKE_SOURCE_DIR}/fonts/SourceSerif4-Semibold.ttf
          ${CMAKE_SOURCE_DIR}/fonts/SourceSerif4-It.ttf
          ${CMAKE_SOURCE_DIR}/fonts/SourceSerif4-SemiboldIt.ttf
)

# Aliased to the root of the module: without this the type is registered under
# the directory it happens to sit in, and the module has no "Main" in it.
set_source_files_properties(gui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)

# The colours, in one place and available to every file in the module.
set_source_files_properties(gui/Ink.qml PROPERTIES
    QT_RESOURCE_ALIAS Ink.qml
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(fretwork
    URI org.kde.fretwork
    VERSION 1.0
    QML_FILES
        gui/Main.qml
        gui/Ink.qml
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

# Without this a software centre will not list the program at all -- the
# .desktop file alone is enough to put it in a menu and nowhere else.
install(FILES io.github.sonicp3l1c4n.fretwork.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
