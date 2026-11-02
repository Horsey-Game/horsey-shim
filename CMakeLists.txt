cmake_minimum_required(VERSION 3.20)
project(SHIM C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(HORSEY_DIR "D:/SteamLibrary/steamapps/common/Horsey Game" CACHE PATH "")
option(HORSEY_INSTALL "" OFF)

include(FetchContent)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
)
FetchContent_MakeAvailable(minhook)

# Lua has no CMake build: download the tarball and compile its sources directly.
FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    SOURCE_SUBDIR none
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua)
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")

add_library(shim SHARED
    src/proxy.cpp
    src/proxy.asm
    src/exports.def
    src/log.cpp
    src/scan.cpp
    src/core.cpp
    src/game.cpp
    src/lua_api.cpp
    res/shim.rc
    ${LUA_SOURCES}
)

set_target_properties(shim PROPERTIES
    OUTPUT_NAME version
    PREFIX ""
)

target_compile_definitions(shim PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(shim PRIVATE ${minhook_SOURCE_DIR}/include ${lua_SOURCE_DIR}/src)
target_link_libraries(shim PRIVATE minhook)

if(HORSEY_INSTALL)
    add_custom_command(TARGET shim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:shim> "${HORSEY_DIR}/version.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/mods" "${HORSEY_DIR}/mods"
    )
endif()
