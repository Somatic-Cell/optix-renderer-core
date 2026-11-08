configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/triangle_smoke.json"
    "${CMAKE_CURRENT_BINARY_DIR}/triangle_smoke.json"
    COPYONLY
)

add_test(
    NAME triangle_gpu_smoke
    COMMAND "${CMAKE_COMMAND}"
        "-DTRIANGLE_EXECUTABLE=$<TARGET_FILE:triangle_cli>"
        "-DTRIANGLE_SMOKE_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/triangle_smoke.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/triangle_smoke.cmake"
)

set_tests_properties(triangle_gpu_smoke PROPERTIES
    LABELS "gpu;triangle"
    TIMEOUT 120
)
