cmake_minimum_required(VERSION 3.16)

project(loxseq LANGUAGES C)

add_library(loxseq
  src/loxseq.c
)

target_include_directories(loxseq
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_features(loxseq PUBLIC c_std_99)

if (MSVC)
  target_compile_options(loxseq PRIVATE /W4)
else()
  target_compile_options(loxseq PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(loxseq_example_minimal examples/minimal.c)
target_link_libraries(loxseq_example_minimal PRIVATE loxseq)

add_executable(loxseq_example_batch_reactor examples/batch_reactor.c)
target_link_libraries(loxseq_example_batch_reactor PRIVATE loxseq)

enable_testing()
add_executable(loxseq_tests tests/test_loxseq.c)
target_link_libraries(loxseq_tests PRIVATE loxseq)
add_test(NAME loxseq_tests COMMAND loxseq_tests)

