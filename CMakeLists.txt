cmake_minimum_required(VERSION 3.14)
project(g-systemctl
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Terminal UI for system service management"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

include(cmake/FetchFTXUI.cmake)

option(BUILD_TESTING "Enable unit testing" OFF)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

file(GLOB_RECURSE SOURCES
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

add_executable(g-systemctl ${SOURCES})

target_include_directories(g-systemctl PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(g-systemctl PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS g-systemctl RUNTIME DESTINATION bin)
