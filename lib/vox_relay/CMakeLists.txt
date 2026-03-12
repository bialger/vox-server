cmake_minimum_required(VERSION 3.25)

add_library(vox_relay STATIC
        delivery_manager.cpp
        relay_service.cpp
)

target_include_directories(vox_relay PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(vox_relay PUBLIC vox_common vox_store)

target_compile_features(vox_relay PUBLIC cxx_std_23)
