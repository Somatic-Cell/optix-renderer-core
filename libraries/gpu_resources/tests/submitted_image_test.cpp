#include <renderer/gpu/submitted_image.hpp>
#include <cuda_driver_wrapper/context.hpp>
#include <cuda_driver_wrapper/device.hpp>
#include <cuda_driver_wrapper/device_buffer.hpp>
#include <cuda_driver_wrapper/device_memory.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <cuda_driver_wrapper/stream.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace
{
namespace cdw = cuda_driver_wrapper;
using Image = renderer::gpu::SubmittedImage<renderer::RGB8>;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class F>
void rejects(F&& function)
{
    try { function(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid ownership was accepted");
}

enum class Resource { pixels, event, context };
struct Release { unsigned int group; Resource resource; };

struct ReleaseLog
{
    std::array<Release, 16> entries{};
    std::size_t count = 0;
    bool overflow = false;

    void append(unsigned int group, Resource resource) noexcept
    {
        if (count == entries.size()) { overflow = true; return; }
        entries[count++] = {group, resource};
    }
};

template<class T, class... Args>
std::shared_ptr<T> tracked(ReleaseLog& log, unsigned int group,
    Resource resource, Args&&... args)
{
    return {new T(std::forward<Args>(args)...),
        [&log, group, resource](T* value) noexcept
        {
            log.append(group, resource);
            delete value;
        }};
}

// These are independent owners. In particular, the Event does not own pixels.
struct Owners
{
    std::shared_ptr<cdw::RetainPrimaryContext> context;
    std::shared_ptr<cdw::DeviceBuffer> pixels;
    std::shared_ptr<cdw::Event> event;

    renderer::gpu::ConstRGB8ImageView image() const
    {
        return {std::as_const(*pixels).view(), 1, 1, sizeof(renderer::RGB8)};
    }
};

Owners make_owners(CUdevice device, ReleaseLog& log, unsigned int group)
{
    Owners owners;
    owners.context = tracked<cdw::RetainPrimaryContext>(
        log, group, Resource::context, device);
    const auto context = owners.context->handle();
    owners.pixels = tracked<cdw::DeviceBuffer>(
        log, group, Resource::pixels, context, sizeof(renderer::RGB8));
    owners.event = tracked<cdw::Event>(log, group, Resource::event, context);

    const renderer::RGB8 pixel{static_cast<std::uint8_t>(group), 87, 169};
    cdw::copy_host_to_device(owners.pixels->view(), std::as_bytes(std::span(&pixel, 1)));
    cdw::Stream stream(context);
    cdw::record_event(*owners.event, stream);
    // Finish test data production before checking ownership and teardown.
    cdw::synchronize_event(*owners.event);
    return owners;
}

Image make_image(CUdevice device, ReleaseLog& log, unsigned int group)
{
    auto owners = make_owners(device, log, group);
    return Image(owners.image(), owners.pixels, owners.event, owners.context, group);
}

void check_image(const Image& image, unsigned int group)
{
    require(image.sequence() == group, "Image metadata changed");
    require(image.is_ready(), "Completion Event did not survive");
    renderer::RGB8 pixel{};
    cdw::copy_device_to_host(std::as_writable_bytes(std::span(&pixel, 1)),
        image.image().memory);
    require(pixel.r == group && pixel.g == 87 && pixel.b == 169,
        "Pixel storage did not survive or changed");
}

void check_release(const ReleaseLog& log, std::size_t offset, unsigned int group)
{
    require(!log.overflow && log.count == offset + 3, "Wrong resource release count");
    for (std::size_t i = offset; i < log.count; ++i)
        require(log.entries[i].group == group, "Released another image's resources");
    require(log.entries[offset + 2].resource == Resource::context,
        "Context owner was released before GPU resources");
    const auto first = log.entries[offset].resource;
    const auto second = log.entries[offset + 1].resource;
    require((first == Resource::pixels && second == Resource::event)
        || (first == Resource::event && second == Resource::pixels),
        "Pixel/Event owners were not both released");
}

void check_lifetime(CUdevice device)
{
    ReleaseLog log; // Must outlive all shared_ptr deleters.
    std::optional<Image> original, copied, moved;
    std::weak_ptr<cdw::DeviceBuffer> pixels;
    std::weak_ptr<cdw::Event> event;
    std::weak_ptr<cdw::RetainPrimaryContext> context;
    {
        auto owners = make_owners(device, log, 1);
        const auto view = owners.image();
        rejects([&] { static_cast<void>(Image(view, {}, owners.event, owners.context, 1)); });
        // An alias of an empty shared_ptr has a pointer, but owns nothing.
        const std::shared_ptr<const void> unowned_pixels(
            std::shared_ptr<const void>{}, owners.pixels.get());
        rejects([&] { static_cast<void>(Image(
            view, unowned_pixels, owners.event, owners.context, 1)); });
        const std::shared_ptr<const cdw::Event> unowned_event(
            std::shared_ptr<const cdw::Event>{}, owners.event.get());
        rejects([&] { static_cast<void>(Image(
            view, owners.pixels, unowned_event, owners.context, 1)); });
        const std::shared_ptr<const void> unowned_context(
            std::shared_ptr<const void>{}, owners.context.get());
        rejects([&] { static_cast<void>(Image(
            view, owners.pixels, owners.event, unowned_context, 1)); });

        original.emplace(view, owners.pixels, owners.event, owners.context, 1);
        copied = original;
        pixels = owners.pixels;
        event = owners.event;
        context = owners.context;
    }
    original.reset();
    require(!pixels.expired() && !event.expired() && !context.expired()
        && log.count == 0, "Copy did not retain all independent owners");
    check_image(*copied, 1);
    moved.emplace(std::move(*copied));
    copied.reset(); // Only destruction/reassignment is required of a moved-from handle.
    check_image(*moved, 1);
    moved.reset();
    require(pixels.expired() && event.expired() && context.expired(),
        "Last image handle did not release its owners");
    check_release(log, 0, 1);
}

void check_assignment(CUdevice device)
{
    ReleaseLog log;
    std::optional<Image> left(make_image(device, log, 2));
    std::optional<Image> right(make_image(device, log, 3));
    *left = *right;
    check_release(log, 0, 2);
    right.reset();
    check_image(*left, 3);

    std::optional<Image> target(make_image(device, log, 4));
    *target = std::move(*left);
    left.reset();
    check_release(log, 3, 4);
    check_image(*target, 3);
    target.reset();
    check_release(log, 6, 3);
}
} // namespace

int main()
{
    try
    {
        cdw::initialize_driver();
        if (cdw::get_device_count() == 0) throw std::runtime_error("A CUDA GPU is required");
        const auto device = cdw::get_device(0);
        check_lifetime(device);
        check_assignment(device);
        std::cout << "Submitted image: independent ownership and assignment lifetime PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
