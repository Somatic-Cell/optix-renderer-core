add_library(renderer_image_processing_settings INTERFACE)
add_library(renderer::image_processing_settings ALIAS renderer_image_processing_settings)
target_compile_features(renderer_image_processing_settings INTERFACE cxx_std_20)
target_include_directories(renderer_image_processing_settings INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(renderer_image_processing STATIC src/output_transform.cpp)
add_library(renderer::image_processing ALIAS renderer_image_processing)
renderer_configure_host(renderer_image_processing)
target_include_directories(renderer_image_processing PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_image_processing PUBLIC
    renderer::image_processing_settings
    renderer::gpu_resources
)

renderer_add_cuda_module(renderer_output_transform_kernel device/output_transform.cu)

target_link_libraries(renderer_output_transform_kernel PRIVATE renderer::image_types)
add_dependencies(renderer_image_processing renderer_output_transform_kernel)
