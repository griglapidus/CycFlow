cmake_minimum_required(VERSION 3.16)
project(ChartViewDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(ChartViewDemo WIN32
    main.cpp
    DataGenerator.h DataGenerator.cpp
    ../ChartWidget/ChartDefs.h ../ChartWidget/ChartDelegate.cpp ../ChartWidget/ChartDelegate.h ../ChartWidget/ChartHeaderView.cpp ../ChartWidget/ChartHeaderView.h ../ChartWidget/ChartModel.cpp ../ChartWidget/ChartModel.h ../ChartWidget/ChartView.cpp ../ChartWidget/ChartView.h ../ChartWidget/ChartWidget.cpp ../ChartWidget/ChartWidget.h
)

target_link_libraries(ChartViewDemo
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
