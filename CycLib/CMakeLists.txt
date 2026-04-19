cmake_minimum_required(VERSION 3.14)
project(CycLib)

option(BUILD_SHARED_LIBS "Build shared library" OFF)

set(SOURCES
    Core/Common.cpp
    Core/PAttr.cpp
    Core/PReg.cpp
    Core/RecBuffer.cpp
    Core/RecRule.cpp
    Core/Record.cpp
    RecordWriter.cpp
    RecordReader.cpp
    RecordConsumer.cpp
    RecordProducer.cpp
    Csv/CsvWriter.cpp
    Cbf/CbfWriter.cpp
    Cbf/CbfReader.cpp
    Cbf/CbfFile.cpp
    Tcp/TcpServer.cpp
    Tcp/TcpServiceClient.cpp
    Tcp/TcpDataSender.cpp
    Tcp/TcpDataReceiver.cpp
)

set(HEADERS
    Core/Common.h
    Core/CycLib_global.h
    Core/PAttr.h
    Core/PReg.h
    Core/RecBuffer.h
    Core/RecRule.h
    Core/Record.h
    Core/CircularBuffer.h
    Core/DynamicChunkBuffer.h
    Core/IRecBufferClient.h
    RecordWriter.h
    RecordReader.h
    RecordConsumer.h
    RecordProducer.h
    Csv/CsvWriter.h
    Cbf/CbfDefs.h
    Cbf/CbfFile.h
    Cbf/CbfWriter.h
    Cbf/CbfReader.h
    Tcp/TcpDefs.h
    Tcp/MessageUtils.h
    Tcp/TcpServer.h
    Tcp/TcpServiceClient.h
    Tcp/TcpDataSender.h
    Tcp/TcpDataReceiver.h
)

add_library(CycLib ${SOURCES} ${HEADERS}
    Core/CycLogger.cpp
    Core/CycLogger.h)

add_library(CycStruct::CycLib ALIAS CycLib)

target_compile_features(CycLib PUBLIC cxx_std_17)

target_compile_definitions(CycLib PUBLIC ASIO_STANDALONE)

if(WIN32)
    target_compile_definitions(CycLib PUBLIC _WIN32_WINNT=0x0A00)
endif()

if(MSVC)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_compile_definitions(CycLib PUBLIC _AMD64_)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_compile_definitions(CycLib PUBLIC _X86_)
    endif()
endif()

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
target_link_libraries(CycLib PUBLIC Threads::Threads asio::asio)

if(WIN32)
    target_link_libraries(CycLib PUBLIC ws2_32 mswsock)
endif()

target_include_directories(CycLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Cyc>
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(CycLib PRIVATE CYCLIB_LIBRARY)
else()
    target_compile_definitions(CycLib PUBLIC CYCLIB_STATIC)
endif()

if(DEFINED HEADER_DEST_DIR)
    set(CYC_HEADER_DEST_DIR "${HEADER_DEST_DIR}/Cyc")
    foreach(HEADER IN LISTS HEADERS)
        get_filename_component(HEADER_DIR "${HEADER}" DIRECTORY)
        set(DEST_DIR "${CYC_HEADER_DEST_DIR}/${HEADER_DIR}")
        add_custom_command(TARGET CycLib POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DEST_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HEADER}" "${DEST_DIR}/"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endforeach()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(TARGET asio_lib)
    install(TARGETS CycLib asio_lib
        EXPORT CycStructTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY "${asio_SOURCE_DIR}/asio/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
else()
    install(TARGETS CycLib
        EXPORT CycStructTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(EXPORT CycStructTargets
    FILE CycStructTargets.cmake
    NAMESPACE CycStruct::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CycStruct
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CycStructConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CycStructConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CycStruct
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CycStructConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CycStruct
)

foreach(HEADER IN LISTS HEADERS)
    get_filename_component(HEADER_DIR "${HEADER}" DIRECTORY)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${HEADER}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Cyc/${HEADER_DIR}"
    )
endforeach()

set_target_properties(CycLib PROPERTIES OUTPUT_NAME "Cyc")
set_target_properties(CycLib PROPERTIES DEBUG_POSTFIX "_d")
