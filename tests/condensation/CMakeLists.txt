cmake_minimum_required(VERSION 3.16)
project(pbc_condensation_tests LANGUAGES CXX)

enable_testing()
find_package(Threads REQUIRED)

add_executable(pbc_condensation_test
    condensation_test.cpp
    ../../src/events/pbc_condense.cpp
)
target_compile_features(pbc_condensation_test PRIVATE cxx_std_17)
target_include_directories(pbc_condensation_test PRIVATE stubs ../../src/events)
target_link_libraries(pbc_condensation_test PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(pbc_condensation_test PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(pbc_condensation_test PRIVATE -Wall -Wextra -Werror)
endif()

option(PBC_TEST_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
if(PBC_TEST_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "PBC_TEST_SANITIZERS requires GCC or Clang")
    endif()
    target_compile_options(pbc_condensation_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pbc_condensation_test PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME condensation_regression COMMAND pbc_condensation_test)
set_tests_properties(condensation_regression PROPERTIES TIMEOUT 30)
