cmake_minimum_required(VERSION 3.15)
project(SysMonitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(SysMonitor WIN32 src/main.cpp)

target_compile_definitions(SysMonitor PRIVATE UNICODE _UNICODE)

target_link_libraries(SysMonitor PRIVATE
    user32 gdi32 gdiplus shell32 iphlpapi winhttp advapi32 ole32
)

if(MSVC)
    target_compile_options(SysMonitor PRIVATE /O2 /EHsc /W3)
    target_link_options(SysMonitor PRIVATE /OPT:REF /OPT:ICF)
else()
    target_compile_options(SysMonitor PRIVATE -O2 -Wall)
endif()
