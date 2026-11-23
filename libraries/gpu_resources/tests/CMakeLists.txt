renderer_add_test(gpu_image_readback
    SOURCES image_readback_test.cpp
    LINK renderer::gpu_resources
    LABELS gpu readback)

renderer_add_test(gpu_submitted_image
    SOURCES submitted_image_test.cpp
    LINK renderer::gpu_resources
    LABELS gpu image lifetime)
