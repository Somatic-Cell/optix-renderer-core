renderer_add_test(cuda_gl_surface
    APPLICATION
    SOURCES surface_test.cpp verify_surface.cpp
    LINK renderer::cuda_gl_display renderer::image_processing renderer::viewer_ui glfw OpenGL::GL
    LABELS viewer opengl gpu surface
    RUN_SERIAL)
