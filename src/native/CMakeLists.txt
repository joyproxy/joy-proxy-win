cmake_minimum_required(VERSION 3.20)
project(JoyProxyNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG master
)
FetchContent_MakeAvailable(minhook)

set(JOYPROXY_COMMON_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)

add_library(joyproxy_common STATIC
    ../common/src/config_shared.cpp
)
target_include_directories(joyproxy_common PUBLIC ${JOYPROXY_COMMON_INCLUDE})

add_library(JoyProxyHook SHARED
    ../hook/src/hook_main.cpp
    ../hook/src/proxy_connector.cpp
)
target_include_directories(JoyProxyHook PRIVATE
    ${JOYPROXY_COMMON_INCLUDE}
    ../hook/include
)
target_link_libraries(JoyProxyHook PRIVATE joyproxy_common minhook ws2_32)
set_target_properties(JoyProxyHook PROPERTIES OUTPUT_NAME "JoyProxyHook")

add_executable(JoyProxyService
    ../service/src/main.cpp
    ../service/src/injector.cpp
    ../service/src/config_mapping.cpp
    ../service/src/pipe_server.cpp
    ../service/src/json_util.cpp
)
target_include_directories(JoyProxyService PRIVATE
    ${JOYPROXY_COMMON_INCLUDE}
    ../service/include
)
target_link_libraries(JoyProxyService PRIVATE joyproxy_common ws2_32 Psapi Shlwapi)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(_DEBUG)
endif()
