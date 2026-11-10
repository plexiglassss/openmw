set(NIFTEST
    niftest.cpp
)
source_group(components\\nif\\tests FILES ${NIFTEST})

# Main executable
openmw_add_executable(niftest
    ${NIFTEST}
)

target_link_libraries(niftest
    components
)

# Temporary audit utility used to export NIF support geometry with OpenMW's own parser.
openmw_add_executable(nifgeometry
    nifgeometry.cpp
)

target_link_libraries(nifgeometry
    components
)

if (WIN32)
    target_sources(niftest PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
    target_sources(nifgeometry PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(niftest PRIVATE --coverage)
    target_link_libraries(niftest gcov)
    target_compile_options(nifgeometry PRIVATE --coverage)
    target_link_libraries(nifgeometry gcov)
endif()

if (NOT APPLE)
    install(TARGETS niftest nifgeometry)
endif()

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(niftest REUSE_FROM components)
    target_precompile_headers(nifgeometry REUSE_FROM components)
endif()
