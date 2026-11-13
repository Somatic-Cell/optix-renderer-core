add_library(renderer_gpu_resources INTERFACE)
add_library(renderer::gpu_resources ALIAS renderer_gpu_resources)
target_include_directories(renderer_gpu_resources INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_gpu_resources INTERFACE
    renderer::image_types
    cuda_driver_wrapper::cuda_driver_wrapper
)