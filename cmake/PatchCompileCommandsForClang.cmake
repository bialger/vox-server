# Patches compile_commands.json so Clang/clangd/clang-tidy use MinGW's libstdc++
# instead of MSVC headers when both MinGW and Visual Studio are installed on Windows.
# Run by CMake at build time (target: patch-compile-commands-for-clang).
#
# Usage: cmake -DCC_FILE=<path> -DFLAGS="..." -DSOURCE_DIR=<path> -P PatchCompileCommandsForClang.cmake
# SOURCE_DIR: if set, copy patched file to <SOURCE_DIR>/compile_commands.json for clangd discovery.

if(NOT DEFINED CC_FILE OR NOT DEFINED FLAGS)
  message(FATAL_ERROR "CC_FILE and FLAGS must be defined")
endif()

if(NOT EXISTS "${CC_FILE}")
  message(STATUS "compile_commands.json not found, skipping patch")
  return()
endif()

file(READ "${CC_FILE}" CONTENT)

# Fix corrupted toolchain path from previous buggy patches
if(CONTENT MATCHES "Program-c")
  string(REPLACE "--gcc-toolchain=C:/Program-c" "--gcc-toolchain=C:/PROGRA~1/mingw64" CONTENT "${CONTENT}")
  file(WRITE "${CC_FILE}" "${CONTENT}")
  message(STATUS "Fixed corrupted gcc-toolchain path (Program-c) in compile_commands.json")
  if(DEFINED SOURCE_DIR)
    configure_file("${CC_FILE}" "${SOURCE_DIR}/compile_commands.json" COPYONLY)
  endif()
  return()
endif()
if(CONTENT MATCHES "mingw64-c")
  string(REPLACE "mingw64-c" "mingw64 -c" CONTENT "${CONTENT}")
  file(WRITE "${CC_FILE}" "${CONTENT}")
  message(STATUS "Fixed corrupted -c flag (mingw64-c) in compile_commands.json")
  if(DEFINED SOURCE_DIR)
    configure_file("${CC_FILE}" "${SOURCE_DIR}/compile_commands.json" COPYONLY)
  endif()
  return()
endif()

if(CONTENT MATCHES "--driver-mode=gcc" AND CONTENT MATCHES "mingw64" AND CONTENT MATCHES "stdlib=libstdc")
  message(STATUS "compile_commands.json already patched for Clang/IDE")
  if(DEFINED SOURCE_DIR)
    configure_file("${CC_FILE}" "${SOURCE_DIR}/compile_commands.json" COPYONLY)
  endif()
  return()
endif()

# Replace " -c " with " FLAGS -c " (explicit space before -c so toolchain path is not merged)
string(REPLACE " -c " " ${FLAGS} -c " CONTENT "${CONTENT}")
file(WRITE "${CC_FILE}" "${CONTENT}")
if(DEFINED SOURCE_DIR)
  configure_file("${CC_FILE}" "${SOURCE_DIR}/compile_commands.json" COPYONLY)
endif()
message(STATUS "Patched compile_commands.json for Clang/IDE (MinGW toolchain)")
