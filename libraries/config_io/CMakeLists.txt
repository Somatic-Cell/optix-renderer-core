add_library(renderer_config_io STATIC src/json_document.cpp)
add_library(renderer::config_io ALIAS renderer_config_io)

renderer_configure_host(renderer_config_io)
target_include_directories(renderer_config_io
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_config_io
    PUBLIC nlohmann_json::nlohmann_json
)
