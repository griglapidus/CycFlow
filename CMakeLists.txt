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

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    message(STATUS "Qt6 found. Qt-based projects are enabled.")
    set(QT_PROJECTS_ENABLED ON)
else()
    message(WARNING "Qt6 not found. Qt-based projects will be skipped.")
    set(QT_PROJECTS_ENABLED OFF)
endif()

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
# Set output directories for each standard build configuration
foreach(CONFIG IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CONFIG} CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_SOURCE_DIR}/build/bin/${CONFIG}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_SOURCE_DIR}/build/lib/${CONFIG}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_SOURCE_DIR}/build/lib/${CONFIG}")
endforeach()

# Fallback base directories (useful for some single-config generators)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/lib")

set(HEADER_DEST_DIR "${CMAKE_SOURCE_DIR}/build/include")
# --------------------------------------

add_subdirectory(CycLib)
if(QT_PROJECTS_ENABLED)
    add_subdirectory(CbfView)
    add_subdirectory(CycBufReceiver)
endif()

option(BUILD_TESTING "Build tests" OFF)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
    if(QT_PROJECTS_ENABLED)
    add_subdirectory(ChartViewTest)
    endif()
    add_subdirectory(CycTestServer)
endif()

