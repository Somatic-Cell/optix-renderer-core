#include <renderer/triangle/viewer_application.hpp>

#include <renderer/render_viewer/viewer.hpp>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <fpng.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef RENDERER_MODULE_DIRECTORY
#error RENDERER_MODULE_DIRECTORY must point to staged GPU modules
#endif

namespace
{
namespace rv = renderer::render_viewer;
using Clock = std::chrono::steady_clock;

enum class Stage
{
    first_image,
    second_image,
    third_image,
    exposure_update,
    idle,
    finished,
};

std::string_view stage_name(Stage stage)
{
    switch (stage)
    {
    case Stage::first_image: return "first image";
    case Stage::second_image: return "second image";
    case Stage::third_image: return "third image / first slot reuse";
    case Stage::exposure_update: return "exposure-only display update";
    case Stage::idle: return "idle updates";
    case Stage::finished: return "finished";
    }
    return "unknown";
}

std::string_view phase_name(rv::ViewerPhase phase)
{
    switch (phase)
    {
    case rv::ViewerPhase::initializing: return "initializing";
    case rv::ViewerPhase::idle: return "idle";
    case rv::ViewerPhase::submitting: return "submitting";
    case rv::ViewerPhase::presenting: return "presenting";
    case rv::ViewerPhase::failed: return "failed";
    }
    return "unknown";
}

struct ExpectedCounts
{
    std::uint64_t received;
    std::uint64_t displayed;
};

ExpectedCounts expected_counts(Stage stage)
{
    switch (stage)
    {
    case Stage::first_image: return {1, 1};
    case Stage::second_image: return {2, 2};
    case Stage::third_image: return {3, 3};
    case Stage::exposure_update:
    case Stage::idle:
    case Stage::finished: return {3, 4};
    }
    throw std::logic_error("Invalid viewer test stage");
}

[[noreturn]] void fail(std::string_view reason, Stage stage,
    const rv::ViewerStatus& status, const rv::ViewerStatistics& counts)
{
    std::ostringstream message;
    message << reason
            << "; stage=" << stage_name(stage)
            << "; phase=" << phase_name(status.phase)
            << "; received=" << counts.received_images
            << "; displayed=" << counts.display_updates
            << "; render_requested=" << status.render_requested
            << "; submission_pending=" << status.submission_pending
            << "; display_pending=" << status.display_pending
            << "; display_dirty=" << status.display_dirty
            << "; has_image=" << status.has_image
            << "; activity=" << status.message;
    throw std::runtime_error(message.str());
}

// The fixed triangle application draws exactly one user texture in addition
// to Dear ImGui's atlas. Check the texture used by the real draw commands,
// after ImGui::Render(), without a test hook or a framebuffer-position query.
GLuint drawn_triangle_texture()
{
    const auto* data = ImGui::GetDrawData();
    if (!data || !data->Valid) throw std::runtime_error("Missing viewer draw data");
    const auto atlas = ImGui::GetIO().Fonts->TexRef.GetTexID();
    ImTextureID image = ImTextureID_Invalid;
    for (const auto* list : data->CmdLists)
        for (const auto& cmd : list->CmdBuffer)
        {
            if (cmd.UserCallback || cmd.ElemCount == 0) continue;
            const auto id = cmd.GetTexID();
            if (id == atlas || id == ImTextureID_Invalid) continue;
            if (image != ImTextureID_Invalid && id != image)
                throw std::runtime_error("Multiple user textures in triangle test");
            image = id;
        }
    if (image == ImTextureID_Invalid) throw std::runtime_error("Rendered triangle texture was not drawn");
    return static_cast<GLuint>(image);
}

using RGB = std::array<std::uint8_t, 3>;
struct DisplaySamples
{
    int width, height;
    RGB background, interior;
};

DisplaySamples verify_drawn_triangle(double exposure_ev)
{
    using Level = void (APIENTRY*)(GLuint, GLint, GLenum, GLint*);
    using Read = void (APIENTRY*)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei,
        GLenum, GLenum, GLsizei, void*);
    const auto level = reinterpret_cast<Level>(glfwGetProcAddress("glGetTextureLevelParameteriv"));
    const auto read = reinterpret_cast<Read>(glfwGetProcAddress("glGetTextureSubImage"));
    if (!level || !read) throw std::runtime_error("Missing texture inspection entry point");
    // This isolated test owns the GL context. Check pack state before using a
    // host destination: an outstanding PBO would reinterpret it as an offset.
    for (GLenum key : std::array<GLenum, 3>{0x88ED, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS})
    {
        GLint value = 0;
        glGetIntegerv(key, &value);
        if (value != 0) throw std::runtime_error("Unexpected OpenGL pixel-pack state");
    }
    const auto texture = drawn_triangle_texture();
    GLint w = 0, h = 0;
    level(texture, 0, GL_TEXTURE_WIDTH, &w);
    level(texture, 0, GL_TEXTURE_HEIGHT, &h);
    if (w < 16 || h < 16) throw std::runtime_error("Invalid triangle texture extent");

    const auto check = [&](int x, int y, std::array<double, 3> linear)
    {
        std::array<unsigned char, 4> rgba{};
        read(texture, 0, x, y, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, 4, rgba.data());
        if (glGetError() != GL_NO_ERROR || rgba[3] != 255)
            throw std::runtime_error("Could not read opaque triangle texel");
        for (std::size_t c = 0; c < linear.size(); ++c)
        {
            const double v = std::clamp(linear[c] * std::exp2(exposure_ev), 0.0, 1.0);
            const double e = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
            if (std::abs(static_cast<int>(rgba[c]) - static_cast<int>(std::lround(255 * e))) > 1)
                throw std::runtime_error("Displayed triangle texel disagrees with expected exposure/color");
        }
        return RGB{rgba[0], rgba[1], rgba[2]};
    };
    const auto background = check(0, 0, {0.02, 0.03, 0.05}); // miss background of this renderer
    const int x = w / 2, y = h / 3;  // strictly inside; sensitive to vertical flips
    const double px = (2.0 * (x + 0.5) / w - 1.0) * w / h;
    const double py = 1.0 - 2.0 * (y + 0.5) / h;
    const double blue = (py + 0.65) / 1.4;
    const double green = 0.5 * (1.0 - blue) + px / 1.5;
    return {w, h, background, check(x, y, {1.0 - green - blue, green, blue})};
}

// The reserved directory belongs only to this test, even in concurrent runs.
class TestDirectory
{
public:
    TestDirectory()
    {
        const auto prefix = "triangle-snapshot-test-" + std::to_string(Clock::now().time_since_epoch().count());
        for (unsigned n = 0; n < 100; ++n)
        {
            auto candidate = std::filesystem::temp_directory_path() / (prefix + "-" + std::to_string(n));
            if (std::filesystem::create_directory(candidate)) { path = std::move(candidate); return; }
        }
        throw std::runtime_error("Could not reserve snapshot test directory");
    }
    ~TestDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;
    std::filesystem::path path;
};

void verify_saved_png(const std::filesystem::path& path, const DisplaySamples& expected)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Snapshot file missing");
    const std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::uint8_t> rgb;
    std::uint32_t w = 0, h = 0, channels = 0;
    if (encoded.empty() || encoded.size() > UINT32_MAX ||
        fpng::fpng_decode_memory(encoded.data(), static_cast<std::uint32_t>(encoded.size()),
            rgb, w, h, channels, 3) != fpng::FPNG_DECODE_SUCCESS ||
        w != static_cast<std::uint32_t>(expected.width) || h != static_cast<std::uint32_t>(expected.height) ||
        channels != 3 || rgb.size() != static_cast<std::size_t>(w) * h * 3)
        throw std::runtime_error("Snapshot is not a valid full-size RGB PNG");
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        const auto i = (static_cast<std::size_t>(y) * w + x) * 3;
        return RGB{rgb[i], rgb[i + 1], rgb[i + 2]};
    };
    if (at(0, 0) != expected.background || at(w / 2, h / 3) != expected.interior)
        throw std::runtime_error("Saved PNG does not match the presented texture at acceptance");
}

DisplaySamples run_viewer_test(const std::filesystem::path& directory)
{
    auto application = renderer::triangle::make_viewer_application(RENDERER_MODULE_DIRECTORY);
    rv::ViewerOptions options;
    options.visible = false;
    options.vsync = false;
    options.validation = true;

    const auto started = Clock::now();
    auto stage_started = started;
    rv::Viewer viewer(std::move(application), options);
    const auto first_path = directory / "at-click.png";
    if (viewer.try_save_snapshot(first_path))
        throw std::runtime_error("Accepted a snapshot before presenting an image");
    DisplaySamples samples{}, at_click{};
    bool verified_file = false;
    Stage stage = Stage::first_image;
    unsigned int idle_updates = 0;
    constexpr unsigned int required_idle_updates = 8;

    const auto advance = [&](Stage next)
    {
        stage = next;
        stage_started = Clock::now();
    };

    while (stage != Stage::finished)
    {
        // The normal executable uses this same update path. Only the scenario
        // and its acceptance criteria belong to this test.
        const auto previous_ui_frame = ImGui::GetFrameCount();
        if (!viewer.update())
            fail("Viewer closed before the scenario finished", stage, viewer.status(), viewer.statistics());

        const auto status = viewer.status();
        const auto counts = viewer.statistics();
        if (status.error)
            std::rethrow_exception(status.error);
        if (status.phase == rv::ViewerPhase::failed)
            fail("Viewer failed without an exception", stage, status, counts);

        const auto now = Clock::now();
        const auto stage_timeout = stage == Stage::first_image
            ? std::chrono::seconds(120) : std::chrono::seconds(30);
        if (now - started > std::chrono::seconds(150) || now - stage_started > stage_timeout)
            fail("Viewer test timed out", stage, status, counts);

        const auto expected = expected_counts(stage);
        if (counts.received_images > expected.received || counts.display_updates > expected.displayed)
            fail("Unexpected extra rendering or display update", stage, status, counts);
        if (counts.display_updates != expected.displayed || ImGui::GetFrameCount() == previous_ui_frame)
            continue;
        if (counts.received_images != expected.received || !status.has_image)
            fail("Published image has unexpected rendering state", stage, status, counts);

        if (stage != Stage::idle)
            samples = verify_drawn_triangle(stage == Stage::exposure_update ? 0.5 : 0.0);

        switch (stage)
        {
        case Stage::first_image:
            advance(Stage::second_image);
            viewer.request_render();
            break;
        case Stage::second_image:
            advance(Stage::third_image);
            viewer.request_render();
            break;
        case Stage::third_image:
        {
            auto settings = viewer.display_settings();
            settings.exposure_ev = 0.5f;
            advance(Stage::exposure_update);
            viewer.set_display_settings(settings);
            // The inspector now requests +0.5 EV, but the front texture is still at 0 EV.
            at_click = samples;
            if (!viewer.try_save_snapshot(first_path))
                throw std::runtime_error("First snapshot rejected");
            if (viewer.try_save_snapshot(directory / "must-not-run.png"))
                throw std::runtime_error("A second save displaced an accepted save");
            break;
        }
        case Stage::exposure_update:
            advance(Stage::idle);
            // Reapplying identical settings must not schedule another transform.
            viewer.set_display_settings(viewer.display_settings());
            break;
        case Stage::idle:
            if (status.phase != rv::ViewerPhase::idle || status.render_requested || status.submission_pending
                || status.display_pending || status.display_dirty)
                fail("Viewer is doing unexpected work while idle", stage, status, counts);
            if (idle_updates < required_idle_updates) ++idle_updates;
            if (!verified_file)
            {
                const auto save = viewer.snapshot_status();
                if (save.phase == rv::SnapshotPhase::failed) throw std::runtime_error(save.message);
                if (save.phase == rv::SnapshotPhase::saved)
                {
                    if (!std::filesystem::equivalent(save.path, first_path))
                        throw std::runtime_error("Wrong snapshot result path");
                    verify_saved_png(save.path, at_click);
                    verified_file = true;
                }
            }
            if (verified_file && idle_updates == required_idle_updates) advance(Stage::finished);
            break;
        case Stage::finished:
            break;
        }
    }

    if (!viewer.try_save_snapshot(directory / "at-close.png"))
        throw std::runtime_error("Could not accept save before close");
    viewer.request_close();
    if (viewer.update())
        fail("Viewer ignored its close request", stage, viewer.status(), viewer.statistics());
    // Viewer destruction, including worker and CUDA/GL resource teardown,
    // completes before the caller checks the output from this second save.
    return samples;
}
}

int main()
{
    try
    {
        TestDirectory directory;
        const auto at_close = run_viewer_test(directory.path);
        verify_saved_png(directory.path / "at-close.png", at_close);
        std::cout << "Triangle viewer: PASS (3 render submissions, 4 display updates, "
                     "8+ idle updates, PNG matches displayed exposure, accepted save survives close)\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Triangle viewer test: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
