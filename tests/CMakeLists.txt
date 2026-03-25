if(VOX_USE_SYSTEM_DEPS)
        find_package(GTest REQUIRED)
else()
        include(FetchContent)

        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG v1.14.0
        )
        # For Windows: Prevent overriding the parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(googletest)
endif()

enable_testing()

add_executable(
        ${PROJECT_NAME}_tests
        test_suites/ProjectIntegrationTestSuite.cpp
        test_suites/ThreadPoolTestSuite.cpp
        test_suites/StoreTestSuite.cpp
        test_suites/AuthTestSuite.cpp
        test_suites/RelayTestSuite.cpp
        test_suites/AttachmentTestSuite.cpp
        test_suites/AdminTestSuite.cpp
        main_tests.cpp
        thread_pool_tests.cpp
        store_tests.cpp
        auth_tests.cpp
        relay_tests.cpp
        attachment_tests.cpp
        admin_tests.cpp
)

target_link_libraries(
        ${PROJECT_NAME}_tests
        GTest::gtest_main
        vox_common
        vox_store
        vox_auth
        vox_relay
        vox_attachments
        vox_admin
)

target_include_directories(${PROJECT_NAME}_tests PUBLIC ${PROJECT_SOURCE_DIR})

include(GoogleTest)

gtest_discover_tests(${PROJECT_NAME}_tests)

if(NOT TESTS_ONLY)
    add_executable(
            ${PROJECT_NAME}_net_tests
            test_suites/ProjectIntegrationTestSuite.cpp
            test_suites/NetApiTestSuite.cpp
            net_api_tests.cpp
    )
    target_link_libraries(
            ${PROJECT_NAME}_net_tests
            GTest::gtest_main
            vox_net
            boost
    )
    target_include_directories(${PROJECT_NAME}_net_tests PUBLIC ${PROJECT_SOURCE_DIR})
    gtest_discover_tests(${PROJECT_NAME}_net_tests)
endif()
