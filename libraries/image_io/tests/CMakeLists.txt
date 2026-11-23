renderer_add_test(new_png
    SOURCES new_png_test.cpp
    LINK renderer::image_io fpng::fpng
    ARGS "${CMAKE_CURRENT_BINARY_DIR}/png_test_data/$<CONFIG>"
    LABELS cpu image)
