renderer_add_test(triangle_config_io
    SOURCES config_io_test.cpp
    LINK renderer::triangle_config renderer::config_io
    ARGS "${CMAKE_CURRENT_BINARY_DIR}/config_io_test_data/$<CONFIG>"
    LABELS cpu config triangle)

renderer_add_test(triangle_gpu_smoke
    COMMAND "${CMAKE_COMMAND}"
        "-DTRIANGLE_EXECUTABLE=$<TARGET_FILE:triangle_cli>"
        "-DTRIANGLE_SMOKE_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/triangle_smoke.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/triangle_smoke.cmake"
    LABELS gpu triangle)
if(TEST triangle_gpu_smoke)
    configure_file(triangle_smoke.json triangle_smoke.json COPYONLY)
endif()

renderer_add_test(triangle_session
    APPLICATION
    SOURCES session_test.cpp
    LINK renderer::triangle renderer::render_execution
    LABELS gpu triangle session)

renderer_add_test(triangle_viewer_gpu_smoke
    APPLICATION
    SOURCES viewer_test.cpp
    LINK renderer::triangle_viewer_application glfw OpenGL::GL fpng::fpng
    LABELS viewer opengl gpu optix
    TIMEOUT 180
    RUN_SERIAL)
