renderer_add_test(viewer_ui_smoke
    APPLICATION
    SOURCES ui_smoke.cpp
    LINK renderer::viewer
    LABELS viewer opengl
    RUN_SERIAL)
