cmake_minimum_required(VERSION 3.14)

project(CycLibTests)

set(CMAKE_OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP true
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

set(BUILD_SHARED_LIBS OFF)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(CycTests
  test_Core.cpp
  test_Cbf.cpp
  test_Csv.cpp
  test_Tcp.cpp
)

target_link_libraries(CycTests PRIVATE CycLib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(CycTests)
set(BUILD_SHARED_LIBS ${CMAKE_OLD_BUILD_SHARED_LIBS})
