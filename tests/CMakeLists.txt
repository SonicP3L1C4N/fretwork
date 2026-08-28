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
