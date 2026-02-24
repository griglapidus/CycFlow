cmake_minimum_required(VERSION 3.16)
project(CbfView LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(CbfView WIN32
    main.cpp
    ChartWidget.h
    ChartWidget.cpp

    ChartDefs.h
    ChartModel.h ChartModel.cpp
    ChartDelegate.h ChartDelegate.cpp
    ChartHeaderView.h ChartHeaderView.cpp
    ChartView.h ChartView.cpp
    ChartHeaderView.h ChartHeaderView.cpp
    CbfReader.h CbfReader.cpp
    MainWindow.h MainWindow.cpp
)

target_link_libraries(CbfView
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    CycLib
)
