add_library(renderer_config_io STATIC src/json_document.cpp)
add_library(renderer::config_io ALIAS renderer_config_io)

target_compile_features(renderer_config_io PUBLIC cxx_std_20)
set_target_properties(renderer_config_io PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(renderer_config_io
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_config_io
    PUBLIC nlohmann_json::nlohmann_json
)
if(MSVC)
    target_compile_options(renderer_config_io PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(renderer_config_io PRIVATE -Wall -Wextra -Wpedantic)
endif()
