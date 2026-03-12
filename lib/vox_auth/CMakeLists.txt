cmake_minimum_required(VERSION 3.25)

include(FetchContent)

FetchContent_Declare(
        argon2
        GIT_REPOSITORY https://github.com/P-H-C/phc-winner-argon2.git
        GIT_TAG 20190702
)
FetchContent_GetProperties(argon2)
if(NOT argon2_POPULATED)
    FetchContent_Populate(argon2)

    add_library(argon2 STATIC
            ${argon2_SOURCE_DIR}/src/argon2.c
            ${argon2_SOURCE_DIR}/src/core.c
            ${argon2_SOURCE_DIR}/src/encoding.c
            ${argon2_SOURCE_DIR}/src/thread.c
            ${argon2_SOURCE_DIR}/src/blake2/blake2b.c
            ${argon2_SOURCE_DIR}/src/ref.c
    )
    target_include_directories(argon2 PUBLIC ${argon2_SOURCE_DIR}/include)
    target_include_directories(argon2 PRIVATE ${argon2_SOURCE_DIR}/src)

    if(NOT MSVC)
        target_compile_options(argon2 PRIVATE -w)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(argon2 PRIVATE pthread)
    endif()
endif()

add_library(vox_auth STATIC
        password_hasher.cpp
        token_manager.cpp
        auth_service.cpp
)

target_include_directories(vox_auth PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(vox_auth PUBLIC vox_common vox_store argon2)

target_compile_features(vox_auth PUBLIC cxx_std_23)
