cmake_minimum_required(VERSION 3.14)

project(CycStruct LANGUAGES CXX)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CYCSTRUCT_MASTER_PROJECT ON)
else()
    set(CYCSTRUCT_MASTER_PROJECT OFF)
endif()

if(NOT TARGET asio::asio)
    include(FetchContent)
    include(GNUInstallDirs)
    FetchContent_Declare(
      asio
      GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
      GIT_TAG        asio-1-36-0
      DOWNLOAD_EXTRACT_TIMESTAMP true
    )
    FetchContent_MakeAvailable(asio)
    add_library(asio_lib INTERFACE)
    target_include_directories(asio_lib INTERFACE
        $<BUILD_INTERFACE:${asio_SOURCE_DIR}/asio/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_definitions(asio_lib INTERFACE ASIO_STANDALONE)
    add_library(asio::asio ALIAS asio_lib)
endif()

if(CYCSTRUCT_MASTER_PROJECT)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        message(STATUS "Qt6 found. Qt-based projects are enabled.")
        set(QT_PROJECTS_ENABLED ON)
    else()
        message(WARNING "Qt6 not found. Qt-based projects will be skipped.")
        set(QT_PROJECTS_ENABLED OFF)
    endif()

    set(COMPILER_NAME ${CMAKE_CXX_COMPILER_ID})
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH_NAME "x64")
    else()
        set(ARCH_NAME "x86")
    endif()

    set(BASE_OUT_DIR "${CMAKE_SOURCE_DIR}/build/${COMPILER_NAME}_${ARCH_NAME}")

    foreach(CONFIG IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${CONFIG} CONFIG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${BASE_OUT_DIR}/bin/${CONFIG}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${BASE_OUT_DIR}/lib/${CONFIG}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${BASE_OUT_DIR}/lib/${CONFIG}")
    endforeach()

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BASE_OUT_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BASE_OUT_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${BASE_OUT_DIR}/lib")

    set(HEADER_DEST_DIR "${BASE_OUT_DIR}/include")
endif()

# --------------------------------------

add_subdirectory(CycLib)

if(CYCSTRUCT_MASTER_PROJECT)
    if(QT_PROJECTS_ENABLED)
        add_subdirectory(CbfView)
        add_subdirectory(CycBufReceiver)
    endif()

    option(BUILD_TESTING "Build tests" OFF)

    if(BUILD_TESTING)
        enable_testing()
        add_subdirectory(tests)
        add_subdirectory(CycTestServer)
    endif()
endif()

