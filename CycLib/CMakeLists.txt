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
)

add_library(CycLib ${SOURCES} ${HEADERS})

target_compile_features(CycLib PUBLIC cxx_std_17)

target_compile_definitions(CycLib PUBLIC ASIO_STANDALONE)

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
target_link_libraries(CycLib PUBLIC Threads::Threads asio::asio)

if(WIN32)
    target_link_libraries(CycLib PUBLIC ws2_32 mswsock)
endif()

target_include_directories(CycLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<BUILD_INTERFACE:${asio_SOURCE_DIR}/asio/include>
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(CycLib PRIVATE CYCLIB_LIBRARY)
else()
    target_compile_definitions(CycLib PUBLIC CYCLIB_STATIC)
endif()

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

include(GNUInstallDirs)
install(TARGETS
    CycLib
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set_target_properties(CycLib PROPERTIES OUTPUT_NAME "Cyc")
set_target_properties(CycLib PROPERTIES DEBUG_POSTFIX "_d")

# Tip: Auto-install in POST_BUILD can slow down development,
# if the project is large, but for a small library this is acceptable.
add_custom_command(TARGET CycLib POST_BUILD
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --config $<CONFIG>
    COMMENT "Auto-installing CycLib..."
)
