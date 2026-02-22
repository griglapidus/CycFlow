cmake_minimum_required(VERSION 3.14)

project(CycStruct LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG        asio-1-36-0
  DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(asio)

if(NOT TARGET asio::asio)
    add_library(asio_lib INTERFACE)
    target_include_directories(asio_lib INTERFACE "${asio_SOURCE_DIR}/include")
    target_compile_definitions(asio_lib INTERFACE ASIO_STANDALONE)
    add_library(asio::asio ALIAS asio_lib)
endif()

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

if(MSVC)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions(_AMD64_)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_compile_definitions(_X86_)
    endif()
endif()

# --- Output Directory Configuration ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/lib)
set(HEADER_DEST_DIR "${CMAKE_SOURCE_DIR}/build/include")
# --------------------------------------

add_subdirectory(CycLib)

option(BUILD_TESTING "Build tests" OFF)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(CbfView)
