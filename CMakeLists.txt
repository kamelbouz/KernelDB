cmake_minimum_required(VERSION 3.15)
project(kerneldb VERSION 1.7.0 LANGUAGES CXX)

# C13 : lier statiquement le runtime GCC sur Windows
# pour que le binaire soit autonome (pas de libgcc_s_seh-1.dll,
# libstdc++-6.dll à côté)
if(WIN32 AND MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(KERNELDB_BUILD_TESTS "Build unit tests" OFF)

# ---- Bibliothèque cœur ----
add_library(kerneldb_core
    src/database.cpp
    src/page.cpp
    src/page_manager.cpp
    src/bplus_tree.cpp
    src/parser.cpp
    src/session.cpp
    src/tokenizer.cpp
    src/catalog.cpp
    src/storage.cpp
    src/buffer_pool.cpp
    src/validation.cpp
    src/lock.cpp
    src/journal.cpp
    src/types.cpp
    src/index_manager.cpp
)

# Nom de la cible exportée : kerneldb::core (au lieu de kerneldb::kerneldb_core)
set_target_properties(kerneldb_core PROPERTIES EXPORT_NAME core)

# Alias pour utilisation interne
add_library(kerneldb::core ALIAS kerneldb_core)

target_include_directories(kerneldb_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# ---- Binaire principal ----
add_executable(kerneldb src/main.cpp)
target_link_libraries(kerneldb PRIVATE kerneldb_core)

# ---------- Benchmark (optionnel) ----------
option(KERNELDB_BUILD_BENCH "Build benchmarks" OFF)
if(KERNELDB_BUILD_BENCH)
    add_executable(bench_storage bench/bench_storage.cpp)
    target_link_libraries(bench_storage PRIVATE kerneldb_core)
endif()

# ---- Installation ----
include(GNUInstallDirs)

install(TARGETS kerneldb
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS kerneldb_core
        EXPORT kerneldbTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT kerneldbTargets
        FILE kerneldbTargets.cmake
        NAMESPACE kerneldb::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kerneldb)

# Génère kerneldbConfig.cmake et kerneldbConfigVersion.cmake
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/kerneldbConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/kerneldbConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kerneldb
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/kerneldbConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/kerneldbConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/kerneldbConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kerneldb)

# ---- Tests ----
if(KERNELDB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- CPack ----
set(CPACK_PACKAGE_NAME "KernelDB")
set(CPACK_PACKAGE_VENDOR "KernelDB Project")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "KernelDB - small embedded SQL database")
set(CPACK_PACKAGE_CONTACT "kamelbouz@example.com")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "KernelDB")
    set(CPACK_NSIS_PACKAGE_NAME "KernelDB")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_MODIFY_PATH "ON")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "kamelbouz@example.com")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION "database")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
endif()

include(CPack)