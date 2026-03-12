cmake_minimum_required(VERSION 3.25)

add_library(vox_admin STATIC
        admin_service.cpp
)

target_include_directories(vox_admin PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(vox_admin PUBLIC vox_common vox_store)

target_compile_features(vox_admin PUBLIC cxx_std_23)
