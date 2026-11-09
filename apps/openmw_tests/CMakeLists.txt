include_directories(SYSTEM ${GTEST_INCLUDE_DIRS})
include_directories(SYSTEM ${GMOCK_INCLUDE_DIRS})

file(GLOB UNITTEST_SRC_FILES
    main.cpp

    options.cpp

    mwworld/teststore.cpp
    mwworld/testcelllookup.cpp
    mwworld/testduration.cpp
    mwworld/testtimestamp.cpp
    mwworld/testptr.cpp
    mwworld/testweather.cpp
    mwworld/testpositioncellgrid.cpp

    mwdialogue/testkeywordsearch.cpp

    mwmechanics/testlevelledlist.cpp

    mwgui/tooltips.cpp
    mwgui/weightedsearch.cpp
    mwgui/test_journaltypography.cpp
    mwgui/testdialogueformatting.cpp
    mwgui/testdialoguecolourcontrols.cpp
    mwgui/testmessageboxstate.cpp
    mwgui/testmessagepositioncontrols.cpp
    mwgui/testresttime.cpp
    mwgui/testcontinuegame.cpp
    mwgui/testmainmenubutton.cpp
    mwgui/testluauilayout.cpp

    mwlua/testobjectcache.cpp
    mwlua/testmagicbindings.cpp

    mwscript/testscripts.cpp

    mwsound/testheadcache.cpp
    mwsound/testwarmqueue.cpp
)

if (MSVC)
    # these TUs take a lot of memory for codegen
    # in CI, they often go over the 8GB memory limit if another cl.exe is running at the same time
    # this option (make sure you're not looking at the docs for the identically-named linker option) stops four codegen threads being spun off
    # it hurts build time ~33% to enable this unnecessarily, though, so target it to the hungriest boys
    set_source_files_properties(
        mwworld/teststore.cpp
        PROPERTIES
        COMPILE_OPTIONS /cgthreads1
    )
endif()

source_group(apps\\openmw-tests FILES ${UNITTEST_SRC_FILES})

openmw_add_executable(openmw-tests ${UNITTEST_SRC_FILES})

target_link_libraries(openmw-tests
    GTest::GTest
    GMock::GMock
    openmw-lib
)

# Fix for not visible pthreads functions for linker with glibc 2.15
if (UNIX AND NOT APPLE)
    target_link_libraries(openmw-tests ${CMAKE_THREAD_LIBS_INIT})
endif()

if (WIN32)
    target_sources(openmw-tests PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-tests PRIVATE --coverage)
    target_link_libraries(openmw-tests gcov)
endif()

target_compile_definitions(openmw-tests
    PRIVATE OPENMW_DATA_DIR=u8"${CMAKE_CURRENT_BINARY_DIR}/data"
            OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}")

# Execute the same real widget fixtures before the full engine LTO link.
# This target has no game/component-library dependency and is not installed.
openmw_add_executable(openmw-controls-tests EXCLUDE_FROM_ALL
    controls-test-main.cpp
    mwgui/testmessagepositioncontrols.cpp
    ../openmw/mwgui/mapiconcontrols.cpp
    ../openmw/mwgui/messagepositioncontrols.cpp
    ../openmw/mwgui/resourceskin.cpp
    ${PROJECT_SOURCE_DIR}/components/mapmarkers/classification.cpp
)
target_link_libraries(openmw-controls-tests GTest::GTest ${MyGUI_LIBRARIES})
target_compile_definitions(openmw-controls-tests
    PRIVATE OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}")
if (WIN32)
    target_sources(openmw-controls-tests PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw-tests PRIVATE
        <boost/program_options/options_description.hpp>

        <gtest/gtest.h>

        <sol/sol.hpp>

        <algorithm>
        <filesystem>
        <fstream>
        <functional>
        <memory>
        <string>
        <vector>
    )
endif()
