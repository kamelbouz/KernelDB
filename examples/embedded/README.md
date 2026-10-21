# KernelDB - Embedded Example

A minimal example of using KernelDB as a library.

## Build

First, install KernelDB (from the KernelDB root):

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    cmake --install build --prefix "$HOME/kerneldb-install"

Then build this example:

    cd examples/embedded
    cmake -B build -DCMAKE_PREFIX_PATH="$HOME/kerneldb-install"
    cmake --build build
    ./build/kerneldb_example

## Use KernelDB in your own CMake project

    find_package(kerneldb REQUIRED)
    target_link_libraries(my_app PRIVATE kerneldb::core)

## Include

    #include "kerneldb/database.hpp"
    #include "kerneldb/types.hpp"

See main.cpp for a complete example.

