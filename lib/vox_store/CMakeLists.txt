cmake_minimum_required(VERSION 3.25)

if(VOX_USE_SYSTEM_DEPS)
        find_package(SQLiteCpp CONFIG REQUIRED)
else()
        include(FetchContent)

        set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "" FORCE)
        set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
        set(SQLITECPP_USE_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
        set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SQLITECPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
                SQLiteCpp
                GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
                GIT_TAG 3.3.3
        )
        FetchContent_MakeAvailable(SQLiteCpp)
endif()

add_library(vox_store STATIC
        database.cpp
        user_repository.cpp
        device_repository.cpp
        session_repository.cpp
        conversation_repository.cpp
        envelope_repository.cpp
        attachment_repository.cpp
        sync_state_repository.cpp
        sdui_repository.cpp
)

target_include_directories(vox_store PUBLIC ${CMAKE_SOURCE_DIR})

# FetchContent exposes target "SQLiteCpp"; distro CMake config often uses "SQLiteCpp::SQLiteCpp".
if(TARGET SQLiteCpp::SQLiteCpp)
        target_link_libraries(vox_store PUBLIC vox_common SQLiteCpp::SQLiteCpp)
else()
        target_link_libraries(vox_store PUBLIC vox_common SQLiteCpp)
endif()

target_compile_features(vox_store PUBLIC cxx_std_23)
