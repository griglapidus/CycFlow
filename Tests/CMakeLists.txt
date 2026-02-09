cmake_minimum_required(VERSION 3.14)

project(CycLibTests)

# 1. Загружаем Google Test автоматически
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 2. Создаем исполняемый файл тестов
# Добавляйте сюда новые файлы с тестами (test_RecBuffer.cpp и т.д.)
add_executable(CycTests
  test_main.cpp
  test_CsvWriter.cpp
)

# 3. Линкуем с вашей библиотекой и GTest
# GTest::gtest_main содержит встроенную функцию main(), поэтому писать её не нужно
target_link_libraries(CycTests PRIVATE CycLib GTest::gtest_main)

# 4. Включаем поддержку обнаружения тестов (для ctest или IDE)
include(GoogleTest)
gtest_discover_tests(CycTests)
