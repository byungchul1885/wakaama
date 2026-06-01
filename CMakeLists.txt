cmake_minimum_required(VERSION 3.21)

project(wakaama C)

option(WAKAAMA_UNIT_TESTS "Enable build of Unit Tests" ON)

include(wakaama.cmake)

if(WAKAAMA_UNIT_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
