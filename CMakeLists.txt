cmake_minimum_required(VERSION 3.14)

set(G_SYSTEMCTL_VERSION "1.0.0" CACHE STRING "g-systemctl release version")

project(g-systemctl
    VERSION ${G_SYSTEMCTL_VERSION}
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
    G_SYSTEMCTL_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(g-systemctl PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    Threads::Threads
)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS g-systemctl RUNTIME DESTINATION bin)

set(CPACK_PACKAGE_NAME "g-systemctl")
set(CPACK_PACKAGE_VENDOR "shakg")
set(CPACK_PACKAGE_CONTACT "shakg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/shakg/g-systemctl")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

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
