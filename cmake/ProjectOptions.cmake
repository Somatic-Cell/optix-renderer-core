# レンダラのホスト側の C++ ターゲットに共通のコンパイル設定を適用する
include_guard(GLOBAL)

function(renderer_configure_host target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES ECC_EXTENSIONS OFF)
    target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/include")
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor
        )
        target_compile_definitions(${target} PRIVATE 
            NOMINMAX WIN32_LEAN_AND_MEAN
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()