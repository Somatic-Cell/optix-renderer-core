renderer_add_application(cuda_gl_surface_test
    SOURCES surface_test.cpp verify_surface.cpp
    LINK renderer::cuda_gl_display renderer::image_processing renderer::viewer glfw OpenGL::GL
)
add_test(NAME cuda_gl_surface COMMAND cuda_gl_surface_test)
set_tests_properties(cuda_gl_surface PROPERTIES
    LABELS "viewer;opengl;gpu;surface" TIMEOUT 120 RUN_SERIAL TRUE)
