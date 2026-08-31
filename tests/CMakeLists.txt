# SPDX-FileCopyrightText: 2026 Gary Bissett <gary.bissett@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

include(ECMAddTests)

ecm_add_test(rationaltest.cpp
    TEST_NAME rationaltest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

ecm_add_test(zipreadertest.cpp
    TEST_NAME zipreadertest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::Archive ZLIB::ZLIB
)

ecm_add_test(editortest.cpp
    TEST_NAME editortest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(fwformattest.cpp
    TEST_NAME fwformattest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::Archive
)

ecm_add_test(gpiftest.cpp
    TEST_NAME gpiftest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::Archive
)

ecm_add_test(notevaluetest.cpp
    TEST_NAME notevaluetest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(fretboardtest.cpp
    TEST_NAME fretboardtest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

ecm_add_test(keytest.cpp
    TEST_NAME keytest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(analysistest.cpp
    TEST_NAME analysistest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(chordtest.cpp
    TEST_NAME chordtest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(pitchdetectortest.cpp
    TEST_NAME pitchdetectortest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(tunertest.cpp
    TEST_NAME tunertest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(gxpresettest.cpp
    TEST_NAME gxpresettest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

ecm_add_test(lv2test.cpp
    TEST_NAME lv2test
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)
# The suite has to know which build it is testing: without lilv the chain
# refuses everything by saying so, and that is a different sentence from the
# one a build with lilv produces for a plugin that is merely absent.
if(Lilv_FOUND)
    target_compile_definitions(lv2test PRIVATE FRETWORK_HAVE_LILV)
endif()

ecm_add_test(midtest.cpp
    TEST_NAME midtest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

ecm_add_test(rendertest.cpp
    TEST_NAME rendertest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

ecm_add_test(playertest.cpp
    TEST_NAME playertest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

ecm_add_test(tablayouttest.cpp
    TEST_NAME tablayouttest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(tabpaintertest.cpp
    TEST_NAME tabpaintertest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(samplertest.cpp
    TEST_NAME samplertest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

ecm_add_test(sfztest.cpp
    TEST_NAME sfztest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

ecm_add_test(swingtest.cpp
    TEST_NAME swingtest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(timelinetest.cpp
    TEST_NAME timelinetest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

ecm_add_test(rigtest.cpp
    TEST_NAME rigtest
    LINK_LIBRARIES Qt6::Test fretworkcore
)

# `session.cpp` is compiled into the test rather than linked, because it
# belongs to the executable: it is the window's own object and the QML module
# that registers it is declared on the application target. Compiling it here is
# what makes the chain editing testable at all, and the alternative -- moving
# it into the library so a test can link it -- would move a GUI object into a
# library that has no GUI in it for the sake of the test.
ecm_add_test(sessiontest.cpp ../src/gui/session.cpp
    TEST_NAME sessiontest
    LINK_LIBRARIES Qt6::Test Qt6::Qml fretworkcore KF6::I18n
)
target_include_directories(sessiontest PRIVATE ${CMAKE_SOURCE_DIR}/src/gui)

# Runtime-loaded via dlopen regardless of whether this build linked
# PipeWire's development package, so unlike the rest of the suite it always
# builds and always runs -- what it finds depends on what this machine has,
# not on what this build was configured with.
ecm_add_test(jacktransporttest.cpp
    TEST_NAME jacktransporttest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(tracksynthtest.cpp
    TEST_NAME tracksynthtest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(audioportstest.cpp
    TEST_NAME audioportstest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(midiinputtest.cpp
    TEST_NAME midiinputtest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(mackietest.cpp
    TEST_NAME mackietest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::I18n
)

ecm_add_test(feedpaktest.cpp
    TEST_NAME feedpaktest
    LINK_LIBRARIES Qt6::Test fretworkcore KF6::Archive KF6::I18n
)

find_program(DESKTOP_FILE_VALIDATE desktop-file-validate)
if(DESKTOP_FILE_VALIDATE)
    add_test(NAME desktopfiletest
        COMMAND ${DESKTOP_FILE_VALIDATE}
                ${CMAKE_SOURCE_DIR}/src/io.github.sonicp3l1c4n.fretwork.desktop
    )
endif()

# --no-net matters: the screenshots are remote URLs, and a test suite has no
# business reaching out to GitHub to run.
find_program(APPSTREAMCLI appstreamcli)
if(APPSTREAMCLI)
    add_test(NAME metainfotest
        COMMAND ${APPSTREAMCLI} validate --pedantic --no-net
                ${CMAKE_SOURCE_DIR}/src/io.github.sonicp3l1c4n.fretwork.metainfo.xml
    )
endif()
