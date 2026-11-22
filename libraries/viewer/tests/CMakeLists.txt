add_executable(viewer_ui_smoke_test ui_smoke.cpp)
renderer_configure_host(viewer_ui_smoke_test)
target_link_libraries(viewer_ui_smoke_test PRIVATE renderer::viewer)
set_target_properties(viewer_ui_smoke_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/viewer_ui_smoke_test")
add_test(NAME viewer_ui_smoke COMMAND viewer_ui_smoke_test)
set_tests_properties(viewer_ui_smoke PROPERTIES
    LABELS "viewer;opengl" TIMEOUT 30 RUN_SERIAL TRUE)
