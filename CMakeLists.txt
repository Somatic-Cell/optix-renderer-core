cmake_minimum_required(VERSION 3.27)

project(renderer VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "A 64-bit host build is required. Use -A x64 with Visual Studio.")
endif()

get_property(_renderer_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _renderer_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

# All runnable files live together, independently of their source directories.
# The explicit configuration expression avoids a second Debug/Release suffix.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

find_package(CUDAToolkit 12.0 REQUIRED)
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "75-virtual" CACHE STRING
        "CUDA virtual architecture used to compile OptiX IR")
endif()

# CMake's CUDA compiler probes also need the conforming MSVC preprocessor.
if(MSVC)
    string(APPEND CMAKE_CUDA_FLAGS_INIT " -Xcompiler=/Zc:preprocessor")
endif()
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio" AND NOT CMAKE_CUDA_COMPILER)
    set(CMAKE_CUDA_COMPILER "${CUDAToolkit_NVCC_EXECUTABLE}" CACHE FILEPATH
        "CUDA compiler")
endif()
enable_language(CUDA)
if(NOT CMAKE_CUDA_COMPILER_ID STREQUAL "NVIDIA")
    message(FATAL_ERROR "OptiX IR compilation requires NVIDIA nvcc.")
endif()
if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 12.0)
    message(FATAL_ERROR "CUDA 12.0 or later is required for this C++20 device code.")
endif()

# Only device modules use the CUDA language. Application code uses the Driver API.
set(CMAKE_CUDA_RUNTIME_LIBRARY None)

include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)
include(cmake/GpuModules.cmake)

add_subdirectory(src/image_processing)
add_subdirectory(src/asset_io)
add_subdirectory(src/optix_backend)
add_subdirectory(renderers/triangle)
add_subdirectory(apps/render_cli)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT render_cli)

message(STATUS "Renderer: CUDA architectures=${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "Renderer: binaries=${CMAKE_BINARY_DIR}/bin/<configuration>")
