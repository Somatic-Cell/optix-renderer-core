add_library(renderer_gpu STATIC src/image_readback.cpp)
add_library(renderer::gpu ALIAS renderer_gpu)
renderer_configure_host(renderer_gpu)
target_include_directories(renderer_gpu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_gpu PUBLIC
    renderer::image_types
    cuda_driver_wrapper::cuda_driver_wrapper
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()