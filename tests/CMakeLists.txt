cmake_minimum_required(VERSION 3.21)

project(lwm2munittests C)

include(../wakaama.cmake)

set(TEST_SOURCES
    helper/log_handler.c
    coap_block1tests.c
    coap_block2tests.c
    coap_parse_message.c
    data_cbor_tests.c
    core_convert_numbers_test.c
    core_composite_tests.c
    core_list_tests.c
    core_logging_tests.c
    core_management_deferred_tests.c
    core_message_size_test.c
    core_observe_tests.c
    core_registration_retry_tests.c
    core_registration_tests.c
    core_reporting_tests.c
    core_transaction_tests.c
    core_uritests.c
    core_utils_tests.c
    data_senml_cbor_tests.c
    data_senml_json_tests.c
    data_tlv_json_lwm2m_data_test.c
    data_tlvtests.c
    tests.h
    unittests.c
)

set(TARGET_NAME lwm2munittests)

add_executable(${TARGET_NAME})
target_sources(${TARGET_NAME} PRIVATE ${TEST_SOURCES})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(${TARGET_NAME} PRIVATE helper/faults.c)
    target_compile_definitions(${TARGET_NAME} PRIVATE WAKAAMA_TEST_FAULTS)
    target_link_options(${TARGET_NAME} PRIVATE
        -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=lwm2m_gettime)
endif()

set_defines(${TARGET_NAME})

# link CUnit
include(FindPkgConfig)
pkg_search_module(CUNIT REQUIRED IMPORTED_TARGET cunit)
target_link_libraries(${TARGET_NAME} PRIVATE PkgConfig::CUNIT)

# Our tests are designed for POSIX systems
target_compile_definitions(${TARGET_NAME} PRIVATE _POSIX_C_SOURCE=200809)
target_compile_definitions(${TARGET_NAME} PRIVATE WAKAAMA_REGISTRATION_TEST_API)

target_sources_wakaama(${TARGET_NAME})

target_link_libraries(${TARGET_NAME} PRIVATE wakaama_command_line wakaama_platform_posix wakaama_transport_testing_fake)

if(SANITIZER)
    target_compile_options(${TARGET_NAME} PRIVATE -fsanitize=${SANITIZER} -fno-sanitize-recover=all)
    target_link_options(${TARGET_NAME} PRIVATE -fsanitize=${SANITIZER} -fno-sanitize-recover=all)
endif()

if(COVERAGE)
    target_compile_options(${TARGET_NAME} PRIVATE --coverage)
    target_link_options(${TARGET_NAME} PRIVATE --coverage)
endif()

# Add our unit tests to the "test" target
add_test(NAME ${TARGET_NAME}_test COMMAND ${TARGET_NAME})
