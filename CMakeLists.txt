cmake_minimum_required(VERSION 3.20)
project(PortKiller CXX)

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
    _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(PortKiller PRIVATE /utf-8 /W3 /permissive-)
# Static CRT: the exe runs without any redistributable.
set_property(TARGET PortKiller PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(PortKiller PRIVATE
    iphlpapi psapi gdiplus comctl32 comdlg32 dwmapi uxtheme shlwapi shell32 advapi32
)
