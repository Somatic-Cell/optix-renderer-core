add_library(renderer_render_execution INTERFACE)
add_library(renderer::render_execution ALIAS renderer_render_execution)
target_include_directories(renderer_render_execution INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(renderer_render_execution INTERFACE renderer::task_execution)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
