cmake_minimum_required(VERSION 3.25)

if(VOX_USE_SYSTEM_DEPS)
        find_package(fmt CONFIG REQUIRED)
        set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
        find_package(spdlog CONFIG REQUIRED)
else()
        include(FetchContent)

        FetchContent_Declare(
                fmt
                GIT_REPOSITORY https://github.com/fmtlib/fmt.git
                GIT_TAG 12.1.0
        )
        FetchContent_MakeAvailable(fmt)

        set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
                spdlog
                GIT_REPOSITORY https://github.com/gabime/spdlog.git
                GIT_TAG v1.17.0
        )
        FetchContent_MakeAvailable(spdlog)
endif()

add_library(vox_common STATIC
        config.cpp
        uuid.cpp
        thread_pool.cpp
        logging.cpp
        hmac_sha256.cpp
        random_bytes.cpp
)

target_include_directories(vox_common PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(vox_common PUBLIC fmt::fmt spdlog::spdlog)
if(WIN32)
        target_link_libraries(vox_common PUBLIC bcrypt)
endif()

target_compile_features(vox_common PUBLIC cxx_std_23)
