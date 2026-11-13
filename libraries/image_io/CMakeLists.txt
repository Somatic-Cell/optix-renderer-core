add_library(renderer_asset_io STATIC src/png_writer.cpp)
add_library(renderer::asset_io ALIAS renderer_asset_io)
renderer_configure_host(renderer_asset_io)
target_include_directories(renderer_asset_io PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_asset_io
    PUBLIC renderer::image_types
    PRIVATE fpng::fpng Threads::Threads
)
