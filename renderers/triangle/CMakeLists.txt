add_library(renderer_triangle STATIC src/pipeline.cpp src/render_session.cpp)
add_library(renderer::triangle ALIAS renderer_triangle)
renderer_configure_host(renderer_triangle)
target_include_directories(renderer_triangle
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/shared"
)
target_link_libraries(renderer_triangle PUBLIC renderer::optix_backend renderer::gpu_resources)
renderer_add_optix_module(renderer_triangle_programs device/programs.cu
    STAGE_AS triangle/programs.optixir)
target_include_directories(renderer_triangle_programs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/shared")
target_link_libraries(renderer_triangle_programs PRIVATE renderer::image_types)
renderer_require_gpu_module(renderer_triangle renderer_triangle_programs
    RENDERER_TRIANGLE_MODULE_PATH)
add_subdirectory(config)
if(RENDERER_BUILD_VIEWER)
    add_subdirectory(viewer)
endif()
add_subdirectory(app)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
