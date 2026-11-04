cmake_minimum_required(VERSION 3.14)

# Release builds pass -DG_SYSTEMCTL_VERSION=<tag>. Otherwise take it from the
# nearest git tag (e.g. 2.0.1, or 2.0.1-3-gabc1234 past it), else 0.0.0-dev.
if(NOT G_SYSTEMCTL_VERSION)
    set(_git_version "")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --match "v[0-9]*"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE _git_version
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    string(REGEX REPLACE "^v" "" _git_version "${_git_version}")
    if(_git_version)
        set(G_SYSTEMCTL_VERSION "${_git_version}")
    else()
        set(G_SYSTEMCTL_VERSION "0.0.0-dev")
    endif()
endif()

# project() only takes numeric X.Y.Z.
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" G_SYSTEMCTL_PROJECT_VERSION "${G_SYSTEMCTL_VERSION}")
if(NOT G_SYSTEMCTL_PROJECT_VERSION)
    set(G_SYSTEMCTL_PROJECT_VERSION "0.0.0")
endif()

project(g-systemctl
    VERSION ${G_SYSTEMCTL_PROJECT_VERSION}
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
find_package(Threads REQUIRED)

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

target_compile_definitions(g-systemctl PRIVATE
    G_SYSTEMCTL_VERSION="${G_SYSTEMCTL_VERSION}"
)

target_link_libraries(g-systemctl PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    Threads::Threads
)

# Release binaries must run on distros with an older libstdc++ than the build host.
if(UNIX AND NOT APPLE)
    target_link_options(g-systemctl PRIVATE -static-libstdc++ -static-libgcc)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS g-systemctl RUNTIME DESTINATION bin)

set(CPACK_PACKAGE_NAME "g-systemctl")
set(CPACK_PACKAGE_VENDOR "shakg")
set(CPACK_PACKAGE_CONTACT "shakg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${G_SYSTEMCTL_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/shakg/g-systemctl")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${G_SYSTEMCTL_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "TGZ;DEB")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "shakg")
set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "g-systemctl is a terminal user interface for browsing, filtering, and controlling system services.")

include(CPack)
