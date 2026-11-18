add_library(renderer_render_viewer STATIC src/application.cpp)
add_library(renderer::render_viewer ALIAS renderer_render_viewer)

renderer_configure_host(renderer_render_viewer)

target_include_directories(renderer_render_viewer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(renderer_render_viewer
    PUBLIC renderer::viewer renderer::gpu_resources renderer::image_processing_settings renderer::task_execution
    PRIVATE renderer::cuda_gl_display renderer::image_processing
)
if(BUILD_TESTING)
    target_sources(renderer_render_viewer PRIVATE tests/ui_smoke.cpp)
    target_include_directories(renderer_render_viewer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_compile_definitions(renderer_render_viewer PRIVATE RENDERER_VIEWER_TESTS=1)
endif()
if(BUILD_TESTING AND RENDERER_ENABLE_GPU_TESTS)
    target_sources(renderer_render_viewer PRIVATE tests/verify_surface.cpp)
    target_compile_definitions(renderer_render_viewer PRIVATE RENDERER_VIEWER_GPU_CHECKS=1)
    target_link_libraries(renderer_render_viewer PRIVATE glfw OpenGL::GL)
endif()