add_library(renderer_render_viewer STATIC src/application.cpp src/viewer.cpp)
add_library(renderer::render_viewer ALIAS renderer_render_viewer)

renderer_configure_host(renderer_render_viewer)

target_include_directories(renderer_render_viewer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(renderer_render_viewer
    PUBLIC 
        renderer::viewer 
        renderer::gpu_resources 
        renderer::image_processing_settings 
        renderer::task_execution
    PRIVATE 
        renderer::render_execution 
        renderer::cuda_gl_display 
        renderer::image_processing
)