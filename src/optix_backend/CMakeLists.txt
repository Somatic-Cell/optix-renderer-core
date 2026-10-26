# 静的ライブラリ renderer_optix_backend の定義
add_library(renderer_optix_backend STATIC 
    error.cpp 
    device_context.cpp
)

# renderer::optix_backend という別名をつける
add_library(renderer::optix_backend ALIAS renderer_optix_backend)
renderer_configure_host(renderer_optix_backend)

# OptiX のヘッダファイルを参照できるようにする
target_include_directories(renderer_optix_backend SYSTEM PUBLIC "${OPTIX_INCLUDE_DIR}")
# CUDA Dirver API ラッパと動的ライブラリ読み込み機能への依存関係を指定
target_link_libraries(renderer_optix_backend
    PUBLIC cuda_driver_wrapper::cuda_driver_wrapper
    PRIVATE ${CMAKE_DL_LIBS}
)