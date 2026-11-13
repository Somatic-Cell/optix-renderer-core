add_library(renderer_gpu_resources STATIC src/image_readback.cpp)
add_library(renderer::gpu_resources ALIAS renderer_gpu_resources)
renderer_configure_host(renderer_gpu_resources)
target_include_directories(renderer_gpu_resources PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_gpu_resources PUBLIC
    renderer::image_types
    cuda_driver_wrapper::cuda_driver_wrapper
)

if(BUILD_TESTING AND RENDERER_ENABLE_GPU_TESTS)
    add_subdirectory(tests)
endif()