add_library(renderer_image_types INTERFACE)
add_library(renderer::image_types ALIAS renderer_image_types)
target_compile_features(renderer_image_types INTERFACE cxx_std_20)
target_include_directories(renderer_image_types INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
