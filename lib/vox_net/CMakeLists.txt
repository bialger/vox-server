cmake_minimum_required(VERSION 3.25)

add_library(vox_net STATIC
        http_dispatch.cpp
        http_session.cpp
        http_listener.cpp
        ws_registry.cpp
        rate_limiter.cpp
)

target_include_directories(vox_net PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(vox_net PUBLIC boost vox_auth vox_relay vox_attachments vox_admin)

target_compile_features(vox_net PUBLIC cxx_std_23)
