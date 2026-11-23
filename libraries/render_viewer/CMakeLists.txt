add_library(renderer_render_viewer 
    STATIC 
        src/application.cpp 
        src/viewer.cpp
        src/snapshot_panel.cpp
)
add_library(renderer::render_viewer ALIAS renderer_render_viewer)

renderer_configure_host(renderer_render_viewer)

target_include_directories(renderer_render_viewer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(renderer_render_viewer
    PUBLIC 
        renderer::viewer_ui 
        renderer::gpu 
        renderer::image_processing_settings 
        renderer::task_execution
        renderer::render_execution 
    PRIVATE 
        renderer::cuda_gl_display 
        renderer::image_processing
        renderer::image_export
)