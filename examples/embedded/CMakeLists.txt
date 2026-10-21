cmake_minimum_required(VERSION 3.15)
project(kerneldb_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(kerneldb REQUIRED)

add_executable(kerneldb_example main.cpp)
target_link_libraries(kerneldb_example PRIVATE kerneldb::core)

