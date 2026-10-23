add_library(renderer_asset_io STATIC png_writer.cpp)
add_library(renderer::asset_io ALIAS renderer_asset_io)

renderer_configure_host(renderer_asset_io)

target_link_libraries(renderer_asset_io PRIVATE fpng::fpng Threads::Threads)