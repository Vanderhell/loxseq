cmake_minimum_required(VERSION 3.16)

project(loxseq VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

option(LOXSEQ_BUILD_TESTS "Build loxseq tests" ON)
option(LOXSEQ_BUILD_EXAMPLES "Build loxseq examples" ON)
option(LOXSEQ_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_library(loxseq
  src/loxseq.c
)

add_library(loxseq::loxseq ALIAS loxseq)

target_include_directories(loxseq
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(loxseq PUBLIC c_std_99)

if (MSVC)
  target_compile_options(loxseq PRIVATE /W4)
  if (LOXSEQ_WARNINGS_AS_ERRORS)
    target_compile_options(loxseq PRIVATE /WX)
  endif()
else()
  target_compile_options(loxseq PRIVATE -Wall -Wextra -Wpedantic)
  if (LOXSEQ_WARNINGS_AS_ERRORS)
    target_compile_options(loxseq PRIVATE -Werror)
  endif()
endif()

if (LOXSEQ_BUILD_EXAMPLES)
  add_executable(loxseq_example_minimal examples/minimal.c)
  target_link_libraries(loxseq_example_minimal PRIVATE loxseq)

  add_executable(loxseq_example_batch_reactor examples/batch_reactor.c)
  target_link_libraries(loxseq_example_batch_reactor PRIVATE loxseq)
endif()

if (LOXSEQ_BUILD_TESTS)
  enable_testing()

  add_executable(loxseq_tests tests/test_loxseq.c)
  target_link_libraries(loxseq_tests PRIVATE loxseq)
  add_test(NAME loxseq_tests COMMAND loxseq_tests)

  add_executable(loxseq_headercheck tests/headercheck.c)
  target_link_libraries(loxseq_headercheck PRIVATE loxseq)
  if (MSVC)
    target_compile_options(loxseq_headercheck PRIVATE /W4 /WX)
  else()
    target_compile_options(loxseq_headercheck PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME loxseq_headercheck COMMAND loxseq_headercheck)
endif()

install(TARGETS loxseq
  EXPORT loxseqTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md LICENSE CHANGELOG.md DESTINATION .)

install(EXPORT loxseqTargets
  NAMESPACE loxseq::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/loxseq
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/loxseqConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/loxseqConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/loxseqConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/loxseq
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/loxseqConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/loxseqConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/loxseq
)

