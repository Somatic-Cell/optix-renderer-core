add_library(renderer_cuda_gl_display STATIC src/surface_presenter.cpp)
add_library(renderer::cuda_gl_display ALIAS renderer_cuda_gl_display)
renderer_configure_host(renderer_cuda_gl_display)
target_include_directories(renderer_cuda_gl_display PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(renderer_cuda_gl_display
    PUBLIC renderer::gpu_resources
    PRIVATE glfw OpenGL::GL
)

if(BUILD_TESTING AND RENDERER_ENABLE_VIEWER_TESTS AND RENDERER_ENABLE_GPU_TESTS)
    add_subdirectory(tests)
endif()