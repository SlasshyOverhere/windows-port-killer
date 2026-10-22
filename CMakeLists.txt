cmake_minimum_required(VERSION 3.20)

# version.txt is the single source of truth; the release workflow bumps it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" PK_VERSION LIMIT_COUNT 1)
string(STRIP "${PK_VERSION}" PK_VERSION)
string(REPLACE "." ";" PK_VERSION_PARTS "${PK_VERSION}")
list(GET PK_VERSION_PARTS 0 PK_VER_MAJOR)
list(GET PK_VERSION_PARTS 1 PK_VER_MINOR)
list(GET PK_VERSION_PARTS 2 PK_VER_PATCH)

project(PortKiller VERSION ${PK_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(WARNING "PortKiller targets MSVC/clang-cl on Windows; other compilers are untested.")
endif()

add_executable(PortKiller WIN32
    src/main.cpp
    src/ports.cpp
    src/app.rc
    src/app.manifest
)

target_compile_definitions(PortKiller PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00 WINVER=0x0A00
    # consumed by main.cpp and by rc.exe (VERSIONINFO in src/app.rc)
    PK_VERSION="${PK_VERSION}"
    PK_VER_MAJOR=${PK_VER_MAJOR}
    PK_VER_MINOR=${PK_VER_MINOR}
    PK_VER_PATCH=${PK_VER_PATCH})
target_compile_options(PortKiller PRIVATE /utf-8 /W3 /permissive-)
# Static CRT: the exe runs without any redistributable.
set_property(TARGET PortKiller PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(PortKiller PRIVATE
    iphlpapi psapi gdiplus comctl32 comdlg32 dwmapi uxtheme shlwapi shell32 advapi32
)

# Drop the exe next to the build dir too, so `build\PortKiller.exe` works with any generator.
add_custom_command(TARGET PortKiller POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:PortKiller>" "${CMAKE_BINARY_DIR}/PortKiller.exe")
