cmake_minimum_required(VERSION 3.25)

project(
        vox-server
        VERSION 1.0.0
        DESCRIPTION "Server side of the Vox messenger, powered by Boost and C++"
        LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)

message(STATUS "Building ${PROJECT_DESCRIPTION} ${PROJECT_NAME}, version ${PROJECT_VERSION}")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if (WIN32)
    set(USER_HOME_DIRECTORY $ENV{USERPROFILE})
else ()
    set(USER_HOME_DIRECTORY $ENV{HOME})
endif ()

message(STATUS "User home directory: ${USER_HOME_DIRECTORY}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
else ()
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    if (WIN32)
        set(CMAKE_CXX_STANDARD_LIBRARIES "-static-libgcc -static-libstdc++ -lwsock32 -lws2_32 ${CMAKE_CXX_STANDARD_LIBRARIES}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
    endif ()
endif ()

# Ensure compile_commands.json is generated for clangd/clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(lib)

if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    if(NOT TESTS_ONLY)
        add_subdirectory(bin)
    endif()
    add_subdirectory(tests)
endif ()

# On Windows with MinGW + VS installed, Clang/clangd picks up MSVC headers by default.
# Patch compile_commands.json so clangd/clang-tidy use MinGW libstdc++ instead.
# Runs at build time; compile_commands.json exists after cmake generate.
# Use PROGRA~1 short path to avoid space-quoting issues when passing to the patch script.
# Set MINGW_TOOLCHAIN env var if your MinGW is elsewhere (default: C:/Program Files/mingw64).
if (WIN32)
    set(_MINGW "C:/Program Files/mingw64")
    if (DEFINED ENV{MINGW_TOOLCHAIN})
        set(_MINGW "$ENV{MINGW_TOOLCHAIN}")
    endif ()
    # Use short path (C:/PROGRA~1/mingw64) to avoid CMake/shell quoting issues with spaces
    string(REPLACE "Program Files" "PROGRA~1" _MINGW_SHORT "${_MINGW}")
    # Force MinGW libstdc++ for clangd (avoids MSVC headers when VS is installed)
    set(_PATCH_FLAGS " --driver-mode=gcc --target=x86_64-w64-windows-gnu --gcc-toolchain=${_MINGW_SHORT} -std=gnu++23 -stdlib=libstdc++ ")
    add_custom_target(patch-compile-commands-for-clang ALL
        COMMAND ${CMAKE_COMMAND}
            -DCC_FILE="${CMAKE_BINARY_DIR}/compile_commands.json"
            -DFLAGS="${_PATCH_FLAGS}"
            -DSOURCE_DIR="${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchCompileCommandsForClang.cmake"
        COMMENT "Patching compile_commands.json for clangd/clang-tidy (MinGW toolchain)"
        )
    add_dependencies(vox_common patch-compile-commands-for-clang)
    unset(_MINGW)
    unset(_PATCH_FLAGS)
endif ()
