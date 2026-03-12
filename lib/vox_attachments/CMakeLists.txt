cmake_minimum_required(VERSION 3.25)

add_library(vox_attachments STATIC
        attachment_service.cpp
)

target_include_directories(vox_attachments PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(vox_attachments PUBLIC vox_common vox_store)

target_compile_features(vox_attachments PUBLIC cxx_std_23)
