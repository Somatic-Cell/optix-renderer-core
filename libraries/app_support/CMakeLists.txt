add_library(renderer_app_support STATIC
    src/cli.cpp
    src/config_json.cpp
)
add_library(renderer::app_support ALIAS renderer_app_support)
renderer_configure_host(renderer_app_support)
target_include_directories(renderer_app_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(renderer_app_support PUBLIC renderer::config_io renderer::export_settings)
