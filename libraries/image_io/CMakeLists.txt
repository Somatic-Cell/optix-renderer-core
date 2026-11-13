add_library(renderer_image_io STATIC src/png_writer.cpp)
add_library(renderer::image_io ALIAS renderer_image_io)
renderer_configure_host(renderer_image_io)
target_include_directories(renderer_image_io PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_image_io
    PUBLIC renderer::image_types
    PRIVATE fpng::fpng Threads::Threads
)
