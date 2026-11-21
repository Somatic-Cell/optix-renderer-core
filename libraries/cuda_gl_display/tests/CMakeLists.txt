add_executable(cuda_gl_surface_test surface_test.cpp verify_surface.cpp)
renderer_configure_host(cuda_gl_surface_test)
target_link_libraries(cuda_gl_surface_test PRIVATE
    renderer::cuda_gl_display renderer::image_processing renderer::viewer
    glfw OpenGL::GL
)
set_target_properties(cuda_gl_surface_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/cuda_gl_surface_test")
renderer_stage_optix_module(cuda_gl_surface_test renderer_output_transform_kernel
    image_processing output_transform.fatbin)
target_compile_definitions(cuda_gl_surface_test PRIVATE
    "RENDERER_MODULE_DIRECTORY=\"$<TARGET_FILE_DIR:cuda_gl_surface_test>/modules\"")
add_test(NAME cuda_gl_surface COMMAND cuda_gl_surface_test)
set_tests_properties(cuda_gl_surface PROPERTIES
    LABELS "viewer;opengl;gpu;surface" TIMEOUT 120 RUN_SERIAL TRUE)
