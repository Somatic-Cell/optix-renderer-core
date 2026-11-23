#include <renderer/image_io/png_writer.hpp>
#include <fpng.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

void check_pixel(const fs::path& path, const std::array<std::uint8_t, 3>& expected)
{
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "Missing saved PNG");
    const std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(input), {}};
    std::uint32_t w = 0, h = 0, channels = 0;
    std::vector<std::uint8_t> rgb;
    require(fpng::fpng_decode_memory(encoded.data(), static_cast<std::uint32_t>(encoded.size()),
        rgb, w, h, channels, 3) == fpng::FPNG_DECODE_SUCCESS, "Invalid PNG");
    require(w == 1 && h == 1 && channels == 3 && rgb.size() == 3, "Wrong PNG layout");
    require(rgb[0] == expected[0] && rgb[1] == expected[1] && rgb[2] == expected[2],
        "Saved pixel differs (possibly overwritten)");
}

void check_new_png(const fs::path& dir)
{
    const std::array<std::uint8_t, 3> red{210, 0, 0}, blue{0, 0, 180};
    const renderer::HostRGB8ImageView  a{1, 1, std::as_bytes(std::span(red))};
    const renderer::HostRGB8ImageView  b{1, 1, std::as_bytes(std::span(blue))};
    const auto target = dir / u8"triangle.png";

    const auto first = renderer::image_io::write_new_png(target, a);
    const auto second = renderer::image_io::write_new_png(target, b);
    require(first == target && second == dir / u8"triangle_0001.png", "Unexpected collision names");
    check_pixel(first, red);
    check_pixel(second, blue);
    bool failed = false;
    try { static_cast<void>(renderer::image_io::write_new_png(dir / "missing" / "out.png", a)); }
    catch (const fs::filesystem_error&) { failed = true; }
    require(failed, "Failure to create the output was not reported");
}
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Expected a test directory");
        const fs::path root(argv[1]);
        fs::create_directories(root);
        // Never remove an already existing user's file, even when run manually.
        for (unsigned i = 0; ; ++i)
        {
            const auto dir = root / std::to_string(i);
            if (!fs::create_directory(dir)) continue;
            struct Cleanup {
                const fs::path& dir;
                ~Cleanup() { std::error_code ignored; fs::remove_all(dir, ignored); }
            } cleanup{dir};
            check_new_png(dir);
            break;
        }
        std::cout << "New PNG: PASS\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
