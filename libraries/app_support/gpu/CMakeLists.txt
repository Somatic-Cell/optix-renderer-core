add_library(renderer_gpu_app_support STATIC src/gpu_device.cpp)
add_library(renderer::gpu_app_support ALIAS renderer_gpu_app_support)
renderer_configure_host(renderer_gpu_app_support)
target_include_directories(renderer_gpu_app_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(renderer_gpu_app_support SYSTEM PRIVATE "${OPTIX_INCLUDE_DIR}")
target_link_libraries(renderer_gpu_app_support
    PUBLIC renderer::app_support CUDA::cuda_driver
    PRIVATE cuda_driver_wrapper::cuda_driver_wrapper
)
