# Staged / MinGW: static, no system paths. System packages (e.g. Docker + apt): shared libs under /usr.
if(VOX_USE_SYSTEM_DEPS AND NOT WIN32)
        set(Boost_NO_SYSTEM_PATHS OFF)
        set(Boost_USE_MULTITHREADED ON)
        set(Boost_USE_STATIC_LIBS OFF)
        set(Boost_USE_STATIC_RUNTIME OFF)
else()
        set(Boost_NO_SYSTEM_PATHS ON)
        set(Boost_USE_MULTITHREADED ON)
        set(Boost_USE_STATIC_LIBS ON)
        set(Boost_USE_STATIC_RUNTIME ON)
endif()
set(BOOST_ALL_DYN_LINK OFF)

if (NOT Boost_ROOT AND EXISTS $ENV{BOOSTROOT})
    set(Boost_ROOT $ENV{BOOSTROOT})
endif()

find_package(Boost REQUIRED)

add_executable(${PROJECT_NAME}
        main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE vox_net boost)
