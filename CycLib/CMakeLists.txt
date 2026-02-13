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
)

add_library(CycLib ${SOURCES} ${HEADERS})

target_compile_features(CycLib PUBLIC cxx_std_17)

target_include_directories(CycLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(CycLib PRIVATE CYCLIB_LIBRARY)
else()
    target_compile_definitions(CycLib PUBLIC CYCLIB_STATIC)
endif()

set(CYC_HEADER_DEST_DIR "${HEADER_DEST_DIR}/Cyc")
add_custom_command(TARGET CycLib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CYC_HEADER_DEST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HEADERS} "${CYC_HEADER_DEST_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Copying headers to ${CYC_HEADER_DEST_DIR}..."
)

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
