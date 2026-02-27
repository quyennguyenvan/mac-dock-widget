cmake_minimum_required(VERSION 3.15)
project(SysMonitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

set(SYSMON_SOURCES
    src/main.cpp
    libs/globals/globals.cpp
    libs/util/util.cpp
    libs/json/json.cpp
    libs/http/http.cpp
    libs/cpu/cpu.cpp
    libs/mem/mem.cpp
    libs/gpu/gpu.cpp
    libs/disk/disk.cpp
    libs/net/net.cpp
    libs/external/external.cpp
    libs/tray/tray.cpp
    libs/gdip/gdip.cpp
    libs/layout/layout.cpp
    libs/draw/draw.cpp
    libs/tooltip/tooltip.cpp
)
add_executable(SysMonitor WIN32 ${SYSMON_SOURCES})

target_include_directories(SysMonitor PRIVATE ${CMAKE_SOURCE_DIR})

target_compile_definitions(SysMonitor PRIVATE UNICODE _UNICODE)

target_link_libraries(SysMonitor PRIVATE
    user32 gdi32 gdiplus shell32 iphlpapi winhttp advapi32 ole32 comctl32 dxgi
)

if(MSVC)
    target_compile_options(SysMonitor PRIVATE /O2 /EHsc /W3)
    target_link_options(SysMonitor PRIVATE /OPT:REF /OPT:ICF)
else()
    target_compile_options(SysMonitor PRIVATE -O2 -Wall)
endif()
