#include <renderer/gpu/image_readback.hpp>
#include <cuda_driver_wrapper/context.hpp>
#include <cuda_driver_wrapper/device.hpp>
#include <cuda_driver_wrapper/device_buffer.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <cuda_driver_wrapper/stream.hpp>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
namespace cdw = cuda_driver_wrapper;
void require(bool b, const char* msg) { if (!b) throw std::runtime_error(msg); }
template<class Exception, class F> void rejects(F&& f)
{
    try { f(); } catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception was not thrown");
}

template<class Pixel>
void check_image(const cdw::Stream& producer_stream, const cdw::Stream& copy_stream,
    const std::array<Pixel, 15>& pixels, std::size_t padding)
{
    constexpr std::uint32_t width = 5, height = 3;
    const auto row = renderer::checked_row_bytes(width, sizeof(Pixel));
    const auto stride = row + padding;
    const auto total = renderer::required_image_bytes(width, height, stride, sizeof(Pixel));
    const auto expected = std::as_bytes(std::span(pixels));
    std::vector<std::byte> pitched(total, std::byte{0xA5});
    for (std::size_t y = 0; y < height; ++y)
        std::memcpy(pitched.data() + y * stride, expected.data() + y * row, row);

    // Owner is declared before both readbacks and survives their destructors.
    cdw::DeviceBuffer input(producer_stream.context(), total);
    cdw::copy_host_to_device(input.view(), pitched);
    cdw::Event produced(producer_stream.context());
    cdw::record_event(produced, producer_stream);
    const renderer::gpu::ConstImageView<Pixel> image{input.view(), width, height, stride};

    renderer::gpu::ImageReadback<Pixel> readback(copy_stream, width, height);
    require(!readback.is_ready(), "Idle readback reported ready");
    rejects<std::logic_error>([&] { static_cast<void>(readback.view()); });
    rejects<std::logic_error>([&] { readback.wait(); });
    auto short_image = image;
    short_image.memory.size_bytes = total - 1;
    rejects<std::invalid_argument>([&] { readback.submit(short_image, produced); });
    require(!readback.is_ready(), "Validation failure consumed the job");
    readback.submit(image, produced);
    // Readiness may already be true here on a fast GPU; do not assume otherwise.
    readback.wait();
    require(readback.is_ready(), "Completed readback not ready");
    auto result = readback.view();
    require(result.width == width && result.height == height, "Dimensions changed");
    require(result.bytes.size() == expected.size(), "Output was not packed");
    require(std::memcmp(result.bytes.data(), expected.data(), expected.size()) == 0,
        "Raw readback changed image bytes");
    rejects<std::logic_error>([&] { readback.submit(image, produced); });

    renderer::gpu::ImageReadback<Pixel> ordered(producer_stream, width, height);
    ordered.submit(image); // writes were completed above; same-stream overload.
    ordered.wait();
    require(std::memcmp(ordered.view().bytes.data(), expected.data(), expected.size()) == 0,
        "Same-stream transfer changed image bytes");

    // The pending object's destructor must finish the transfer while input lives.
    { renderer::gpu::ImageReadback<Pixel> pending(copy_stream, width, height);
      pending.submit(image, produced); }
}
} // namespace
int main()
{
    try
    {
        cdw::initialize_driver();
        if (cdw::get_device_count() == 0) throw std::runtime_error("A CUDA GPU is required");
        cdw::RetainPrimaryContext context(cdw::get_device(0));
        cdw::Stream producer(context.handle()), copy(context.handle());
        std::array<renderer::RGB8, 15> bytes{};
        std::array<renderer::RGB32f, 15> floats{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            bytes[i] = {static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(255 - i), 129};
            floats[i] = {-static_cast<float>(i), 0.25f, 10000.0f + static_cast<float>(i)};
        }
        floats[0].r = std::bit_cast<float>(std::uint32_t{0x7fc01234}); // preserve NaN bits
        floats[1].g = (std::numeric_limits<float>::infinity)();
        floats[2].b = -0.0f;
        check_image(producer, copy, bytes, 0);
        check_image(producer, copy, bytes, 7);
        check_image(producer, copy, floats, 0);
        check_image(producer, copy, floats, 16);
        std::cout << "GPU image readback: RGB8/RGB32f, packed/pitched PASS\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
