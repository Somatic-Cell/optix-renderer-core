#pragma once 

#include <renderer/render_viewer/application.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <string>

namespace renderer::render_viewer
{

struct ViewerOptions{
    bool visible = true;
    bool vsync = true;
    bool validation = false;
};

enum class ViewerPhase
{
    initializing,
    idle,
    submitting,
    presenting,
    failed
};

struct ViewerStatus
{
    ViewerPhase phase = ViewerPhase::initializing;
    bool render_requested = false;
    bool submission_pending = false;
    bool display_pending = false;
    bool display_dirty = false;
    bool has_image = false;
    std::string message;
    std::exception_ptr error;
};

struct ViewerStatistics
{
    std::uint64_t received_images = 0;
    std::uint64_t display_updates = 0;
};

class Viewer final
{
public:
    explicit Viewer(Application application, const ViewerOptions& options = {});
    ~Viewer() noexcept;

    // copy move の禁止
    Viewer(const Viewer&)               = delete;
    Viewer& operator=(const Viewer&)    = delete;
    Viewer(Viewer &&)                   = delete;
    Viewer& operator=(Viewer&&)         = delete;

    [[nodiscard]] bool update();

    void request_render();
    void set_display_settings(const image_processing::OutputTransformSettings& settings);
    [[nodiscard]] image_processing::OutputTransformSettings display_settings() const;
    void request_close();

    [[nodiscard]] ViewerStatus status() const;
    [[nodiscard]] ViewerStatistics statistics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace renderer::render_viewer