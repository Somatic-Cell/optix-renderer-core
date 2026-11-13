add_library(renderer_export_settings INTERFACE)
add_library(renderer::export_settings ALIAS renderer_export_settings)
target_include_directories(renderer_export_settings INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_export_settings INTERFACE
    renderer::image_processing_settings
)

add_library(renderer_output STATIC src/srgb8_readback.cpp)
add_library(renderer::output ALIAS renderer_output)
renderer_configure_host(renderer_output)
target_include_directories(renderer_output PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_output PUBLIC
    renderer::export_settings
    renderer::image_processing
    renderer::gpu_resources
)