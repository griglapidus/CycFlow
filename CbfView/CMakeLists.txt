cmake_minimum_required(VERSION 3.16)
project(CbfView VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(CbfView WIN32
    main.cpp
    CbfReader.h CbfReader.cpp
    MainWindow.h MainWindow.cpp
    ../ChartWidget/ChartDefs.h
    ../ChartWidget/ChartDelegate.cpp
    ../ChartWidget/ChartDelegate.h
    ../ChartWidget/ChartHeaderView.cpp
    ../ChartWidget/ChartHeaderView.h
    ../ChartWidget/ChartModel.cpp
    ../ChartWidget/ChartModel.h
    ../ChartWidget/ChartView.cpp
    ../ChartWidget/ChartView.h
    ../ChartWidget/ChartWidget.cpp
    ../ChartWidget/ChartWidget.h
    ../resources/Icon.qrc
)

target_link_libraries(CbfView
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    CycLib
)

include("${CMAKE_SOURCE_DIR}/cmake/SetupIcons.cmake")
setup_app_icons(CbfView
                "${CMAKE_SOURCE_DIR}/resources"
                "icon"
                "${CMAKE_SOURCE_DIR}/cmake")
