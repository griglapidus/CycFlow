cmake_minimum_required(VERSION 3.14)

project(CycStruct LANGUAGES CXX)

# --- Output Directory Configuration ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/lib)
set(HEADER_DEST_DIR "${CMAKE_SOURCE_DIR}/build/include")
# --------------------------------------

add_subdirectory(CycLib)

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
