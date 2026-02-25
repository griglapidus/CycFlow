cmake_minimum_required(VERSION 3.16)
project(CycTestServer LANGUAGES CXX)

# Require C++17 as used in CycLib
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find threading support (required by asio and std::thread)
find_package(Threads REQUIRED)

# If ASIO is installed system-wide or provided via a package manager (like vcpkg/Conan)
# find_package(asio REQUIRED)

add_executable(CycTestServer
    main.cpp
)

# -----------------------------------------------------------------------------
# TODO: Adjust the CycLib include directories and linking based on your setup.
# If CycLib is built in the same CMake workspace via add_subdirectory(), 
# you can just link it directly (e.g., target_link_libraries(... CycLib)).
# -----------------------------------------------------------------------------

# Example: If CycLib headers are in a directory relative to this project
# target_include_directories(CycTestServer PRIVATE ../CycLib/include)

target_link_libraries(CycTestServer
    PRIVATE
    CycLib
    Threads::Threads
    # asio::asio      # Uncomment if using find_package(asio)
    # CycLib          # Uncomment and replace with your actual CycLib target name
)
