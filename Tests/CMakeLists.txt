cmake_minimum_required(VERSION 3.14)

project(CycLibTests)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(CycTests
  test_main.cpp
  test_CsvWriter.cpp
  CbfTest.cpp
  CbfWriterTest.cpp
  CbfReaderTest.cpp
)

target_link_libraries(CycTests PRIVATE CycLib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(CycTests)
