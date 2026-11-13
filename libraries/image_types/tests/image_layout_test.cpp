#include <renderer/image_types/image.hpp>
#include <renderer/image_types/image_layout.hpp>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool b, const char* message)
{ if (!b) throw std::runtime_error(message); }
template<class E, class F> void rejects(F&& f)
{
    try { f(); } catch (const E&) { return; }
    throw std::runtime_error("Expected exception was not thrown");
}
}
int main()
{
    try
    {
        using namespace renderer;
        require(checked_row_bytes(800, sizeof(RGB8)) == 2400, "RGB8 row");
        require(packed_image_bytes(800, 600, sizeof(RGB8)) == 1440000, "RGB8 size");
        require(packed_image_bytes(800, 600, sizeof(RGB32f)) == 5760000, "RGB32f size");
        require(required_image_bytes(5, 3, 32, sizeof(RGB8)) == 79, "Pitched final row");
        require(required_image_bytes(5, 1, 4096, sizeof(RGB8)) == 15, "Single row");
        rejects<std::invalid_argument>([] { static_cast<void>(checked_row_bytes(0, 3)); });
        rejects<std::invalid_argument>([] { static_cast<void>(checked_row_bytes(1, 0)); });
        rejects<std::invalid_argument>([] { static_cast<void>(packed_image_bytes(5, 0, 3)); });
        rejects<std::invalid_argument>([] { static_cast<void>(required_image_bytes(5, 3, 14, 3)); });
        const auto max = (std::numeric_limits<std::size_t>::max)();
        rejects<std::length_error>([&] { static_cast<void>(checked_row_bytes(2, max)); });
        rejects<std::length_error>([&] { static_cast<void>(required_image_bytes(1, 2, max, 1)); });
        rejects<std::length_error>([&] { static_cast<void>(required_image_bytes(1, 3, max / 2 + 1, 1)); });
        require(required_image_bytes(1, 2, max - 1, 1) == max, "Exact size_t boundary");
        RGB32f value{-1.0f, 0.5f, 1000.0f}, restored{};
        const RGB32fImageView view{1, 1, std::as_bytes(std::span(&value, 1))};
        std::memcpy(&restored, view.bytes.data(), sizeof(restored));
        require(restored.r == value.r && restored.g == value.g && restored.b == value.b,
            "Image view must not transform samples");
        std::cout << "Image layout: 14 checks passed\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
