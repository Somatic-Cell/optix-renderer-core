add_library(renderer_export_settings STATIC src/export_settings.cpp)
add_library(renderer::export_settings ALIAS renderer_export_settings)
renderer_configure_host(renderer_export_settings)
target_include_directories(renderer_export_settings PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_export_settings PUBLIC
    renderer::image_processing_settings
    renderer::image_io
)

add_library(renderer_image_export STATIC src/rgb8_export_job.cpp)
add_library(renderer::image_export ALIAS renderer_image_export)
renderer_configure_host(renderer_image_export)
target_include_directories(renderer_image_export PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_image_export PUBLIC
    renderer::export_settings
    renderer::image_processing
    renderer::gpu_resources
)