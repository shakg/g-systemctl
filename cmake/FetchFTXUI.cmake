include(FetchContent)

# The patch backports FTXUI 6's ESC parsing: v5 reads one byte too many
# after ESC+key, so two Alt keys in one read (ESC j ESC j) became an unknown
# event and a plain "j". Skipped when already applied (re-configure).
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v5.0.0
    PATCH_COMMAND sh -c "git apply --reverse --check '${CMAKE_CURRENT_LIST_DIR}/ftxui-alt-key.patch' 2>/dev/null || git apply '${CMAKE_CURRENT_LIST_DIR}/ftxui-alt-key.patch'"
)

set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# Keep FTXUI headers and static libs out of `make install` and the release packages.
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(ftxui)
