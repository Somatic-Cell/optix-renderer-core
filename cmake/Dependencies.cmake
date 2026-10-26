# 重複して呼び出しても 1 回しか実行しない
include_guard(GLOBAL) 

include(FetchContent)
find_package(Threads REQUIRED)

# -----------------------
# OptiX SDK
# -----------------------

# OptiX のインストール場所を探す
set(OPTIX_ROOT "" CACHE PATH "OptiX SDK directory containing include/optix.h")
find_path(OPTIX_INCLUDE_DIR
    NAMES optix.h
    HINTS "${OPTIX_ROOT}" "$ENV{OPTIX_ROOT}" # CMake の変数と OS の環境変数のどちらも探索の候補とする
    PATH_SUFFIXES include
)

# OptiX が見つからない場合，中止
if(NOT OPTIX_INCLUDE_DIR)
    message(FATAL_ERROR "OptiX headers were not found. Configure with -DOPTIX_ROOT=${OPTIX_ROOT}.")
endif()

# OptiX のバージョンを読み取る
# STRINGS: ファイルから文字列の行を取得
# REGEX: その正規表現に一致する行だけを取得
# #define OPTIX_VERSION (数字が一つ以上) の行を探す
file(STRINGS "${OPTIX_INCLUDE_DIR}/optix.h" _renderer_optix_version_line
    REGEX "^#define[ \t]+OPTIX_VERSION[ \t]+[0-9]+")

# 一致する行がない場合，中止
if(NOT _renderer_optix_version_line)
    message(FATAL_ERROR "Cannot red OPTIX_VERSION from ${OPTIX_INCLUDE_DIR}/optix.h")
endif()

# バージョン番号の抽出
# 数字の部分だけを抜き出す
string(REGEX REPLACE ".*OPTIX_VERSION[ \t]+([0-9]+).*" "\\1"
    _renderer_optix_version "${_renderer_optix_version_line}")

# 8.0 以前のバージョンは拒否して停止
if(_renderer_optix_version LESS 80000)
    message(FATAL_ERROR "This implementation requires OptiX 8.0 or later.")
endif()
message(STATUS "Renderer: OptiX header version=${_renderer_optix_version}")


# -----------------------
# cuda-driver-wrapper
# -----------------------
# 通常はテストしない
set(CUDA_DRIVER_WRAPPER_BUILD_TESTS OFF CACHE BOOL
    "Build cuda-driver-wrapper tests" FORCE)

add_subdirectory(
    "${PROJECT_SOURCE_DIR}/ext/cuda-driver-wrapper" 
    "${CMAKE_CURRENT_BINARY_DIR}/cuda_driver_wrapper-build"
)

if(MSVC)
    target_compile_definitions(cuda_driver_wrapper PRIVATE /Zc:preprocessor)
endif()

# -----------------------
# fpng
# -----------------------

set(_renderer_fpng_source "${PROJECT_SOURCE_DIR}/ext/fpng")

add_library(renderer_fpng STATIC
    "${_renderer_fpng_source}/src/fpng.cpp"
)

add_library(fpng::fpng ALIAS renderer_fpng)

target_include_directories(renderer_fpng SYSTEM PUBLIC
    "${_renderer_fpng_source}/src"
)
target_compile_features(renderer_fpng PRIVATE cxx_std_11)
set_target_properties(renderer_fpng PROPERTIES CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rendrer_fpng PRIVATE -fno-strict-aliasing)

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i.86|x86)$")
        target_compile_options(render_fpng PRIVATE -msse4.1 -mpclmul)
    else()
        target_compile_options(rendrer_fpng PRIVATE FPNG_NO_SSE=1)
    endif()
endif()
