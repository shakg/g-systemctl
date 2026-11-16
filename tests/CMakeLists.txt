include(GoogleTest)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE CORE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/core/*.cpp
)

file(GLOB_RECURSE PLATFORM_SOURCES
    ${PROJECT_SOURCE_DIR}/src/platform/*.cpp
)

file(GLOB_RECURSE CONTAINER_SOURCES
    ${PROJECT_SOURCE_DIR}/src/container/*.cpp
)

add_executable(g-systemctl-tests ${TEST_SOURCES} ${CORE_SOURCES} ${PLATFORM_SOURCES} ${CONTAINER_SOURCES})

target_include_directories(g-systemctl-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(g-systemctl-tests PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    GTest::gtest_main
)

gtest_discover_tests(g-systemctl-tests)

add_test(
    NAME cli-argument-errors
    COMMAND ${CMAKE_COMMAND}
        -DGSYSTEMCTL_BINARY=$<TARGET_FILE:g-systemctl>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_argument_errors_test.cmake
)
