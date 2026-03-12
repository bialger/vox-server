cmake_minimum_required(VERSION 3.25)

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

add_library(vox_store STATIC
        database.cpp
        user_repository.cpp
        device_repository.cpp
        session_repository.cpp
        conversation_repository.cpp
        envelope_repository.cpp
        attachment_repository.cpp
)

target_include_directories(vox_store PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(vox_store PUBLIC vox_common SQLiteCpp)

target_compile_features(vox_store PUBLIC cxx_std_23)
