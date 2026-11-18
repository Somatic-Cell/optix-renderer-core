#include "verify_surface.hpp"
#include <algorithm>
#include <cmath>
#include <cuda_driver_wrapper/device_memory.hpp>
#include <cuda_driver_wrapper/device_buffer.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <cuda_driver_wrapper/stream.hpp>
#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>

#if defined(_WIN32)
#define RENDERER_GL_CALL __stdcall
#else
#define RENDERER_GL_CALL
#endif

namespace renderer::render_viewer::tests
{
void verify_surface(unsigned int texture, const gpu::SubmittedRGB32fImage& submitted,
    const image_processing::OutputTransform& transform,
    const image_processing::OutputTransformSettings& settings)
{
    const auto source = submitted.image();
    image_processing::validate_output_transform_settings(settings);
    static_cast<void>(gpu::validate_image_view(source, source.memory.context));
    using GetTextureSubImage = void (RENDERER_GL_CALL*)(GLuint, GLint,
        GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, GLsizei, void*);
    const auto address = glfwGetProcAddress("glGetTextureSubImage");
    if (!address) throw std::runtime_error("glGetTextureSubImage is required for the V2 test");
    const auto read_texture = reinterpret_cast<GetTextureSubImage>(address);
    using BindBuffer = void (RENDERER_GL_CALL*)(GLenum, GLuint);
    const auto bind_address = glfwGetProcAddress("glBindBuffer");
    if (!bind_address) throw std::runtime_error("glBindBuffer is required for the V2 test");
    const auto bind_buffer = reinterpret_cast<BindBuffer>(bind_address);
    constexpr GLenum pack_buffer = 0x88EB, pack_buffer_binding = 0x88ED;

    struct Restore
    {
        BindBuffer bind;
        GLint buffer = 0, alignment = 4, row_length = 0, skip_rows = 0, skip_pixels = 0;
        explicit Restore(BindBuffer fn) : bind(fn)
        {
            glGetIntegerv(pack_buffer_binding, &buffer);
            glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
            glGetIntegerv(GL_PACK_ROW_LENGTH, &row_length);
            glGetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows);
            glGetIntegerv(GL_PACK_SKIP_PIXELS, &skip_pixels);
        }
        ~Restore()
        {
            bind(pack_buffer, static_cast<GLuint>(buffer));
            glPixelStorei(GL_PACK_ALIGNMENT, alignment);
            glPixelStorei(GL_PACK_ROW_LENGTH, row_length);
            glPixelStorei(GL_PACK_SKIP_ROWS, skip_rows);
            glPixelStorei(GL_PACK_SKIP_PIXELS, skip_pixels);
        }
    } restore(bind_buffer);
    bind_buffer(pack_buffer, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);

    const auto width = source.width;
    const auto height = source.height;
    const std::array<std::array<std::uint32_t, 2>, 8> points{{
        {0, 0}, {width - 1, 0}, {0, height - 1}, {width - 1, height - 1},
        {width / 2, height / 2}, {width / 3, height / 2},
        {width - width / 3 - 1, height / 2}, {width / 2, height / 3}
    }};

    namespace cdw = cuda_driver_wrapper;
    cdw::Stream stream(source.memory.context);
    cdw::DeviceBuffer reference(source.memory.context, points.size() * sizeof(RGB8));
    cdw::Event done(source.memory.context, static_cast<unsigned int>(CU_EVENT_DISABLE_TIMING) | static_cast<unsigned int>(CU_EVENT_BLOCKING_SYNC));
    try
    {
        cdw::stream_wait_event(stream, submitted.completion_event());
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const auto& point = points[i];
            const auto offset = static_cast<std::size_t>(point[1]) * source.row_stride_bytes
                + static_cast<std::size_t>(point[0]) * sizeof(RGB32f);
            const gpu::ConstRGB32fImageView input{cdw::subview(source.memory, offset, sizeof(RGB32f)),
                1, 1, sizeof(RGB32f)};
            const gpu::RGB8ImageView output{cdw::subview(reference.view(), i * sizeof(RGB8), sizeof(RGB8)),
                1, 1, sizeof(RGB8)};
            transform.enqueue(input, output, stream, settings);
        }
        cdw::record_event(done, stream);
        cdw::synchronize_event(done); // diagnostics ONLY
    }
    catch (...) { cdw::synchronize_stream(stream); throw; }
    std::array<RGB8, 8> reference_bytes{};
    cdw::copy_device_to_host(std::as_writable_bytes(std::span(reference_bytes)), reference.view());
    const double exposure = std::exp2(static_cast<double>(settings.exposure_ev));
    const auto encode = [exposure](float value)
    {
        const double linear = std::clamp(static_cast<double>(value) * exposure, 0.0, 1.0);
        const double encoded = linear <= 0.0031308 ? 12.92 * linear
            : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
        return static_cast<int>(std::lround(std::clamp(encoded, 0.0, 1.0) * 255.0));
    };
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto& point = points[i];
        const auto offset = static_cast<std::size_t>(point[1]) * source.row_stride_bytes
            + static_cast<std::size_t>(point[0]) * sizeof(RGB32f);
        RGB32f linear{};
        cuda_driver_wrapper::copy_device_to_host(std::as_writable_bytes(std::span(&linear, 1)),
            cuda_driver_wrapper::subview(source.memory, offset, sizeof(linear)));
        std::array<std::uint8_t, 4> actual{};
        read_texture(texture, 0, static_cast<GLint>(point[0]), static_cast<GLint>(point[1]), 0,
            1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(actual.size()), actual.data());
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL diagnostic pixel read failed");
        const bool finite = std::isfinite(linear.r) && std::isfinite(linear.g) && std::isfinite(linear.b);
        const std::array<int, 3> expected = finite
            ? std::array<int, 3>{encode(linear.r), encode(linear.g), encode(linear.b)}
            : std::array<int, 3>{0, 0, 0};
        for (std::size_t c = 0; c < expected.size(); ++c)
            if (std::abs(static_cast<int>(actual[c]) - expected[c]) > 1)
                throw std::runtime_error("Display texture differs from the reference transform");
        const auto reference_pixel = reference_bytes[i];
        if (actual[0] != reference_pixel.r || actual[1] != reference_pixel.g
            || actual[2] != reference_pixel.b || actual[3] != 255)
            throw std::runtime_error("Surface differs from the GPU linear-output kernel or alpha is not opaque");
    }
    std::cout << "GPU surface transform: PASS (8 GPU linear/surface pairs exact + analytic reference <=1 code value)\n";
}
}
#undef RENDERER_GL_CALL
