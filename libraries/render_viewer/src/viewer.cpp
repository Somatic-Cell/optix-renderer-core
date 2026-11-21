#include <renderer/render_viewer/viewer.hpp>
#include "snapshot_panel.hpp"
#include <renderer/image_export/snapshot_writer.hpp>
#include <renderer/cuda_gl_display/sufrece_presenter.hpp>
#include <renderer/image_processing/output_transform.hpp>
#include <renderer/render_execution/render_execution.hpp>
#include <renderer/viewer/window.hpp>
#include <renderer/viewer/workspace.hpp>
#include <cuda_driver_wrapper/event.hpp>

#include <imgui.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace renderer::render_viewer
{

namespace
{

namespace tasks = task_execution;
namespace cdw = cuda_driver_wrapper;

struct DisplayProgram
{
    // 
    std::shared_ptr<const void> context_owner;
    // 
    image_processing::OutputTransform transform;

    DisplayProgram(SourceInfo info, const std::filesystem::path& path)
        : context_owner(std::move(info.context_lifetime)), transform(info.context, path) {}
};

// 1 回のタスクの結果
// GPU に投入済みの画像生成処理を参照するオブジェクトを返す
struct Reply
{
    gpu::SubmittedRGB32fImage image;
    std::shared_ptr<DisplayProgram> display;
};

// 
struct Backend
{
    std::unique_ptr<FrameSource> source; // persistent renderer, destroyed on worker
    std::shared_ptr<DisplayProgram> display;

    bool try_advance() { return source->try_advance(); }
    bool collect() { return source->collect(); };

    std::optional<Reply> take_latest_image()
    {
        auto image = source->take_latest_image();
        if(!image)
        {
            return std::nullopt;
        }
        return Reply{std::move(*image), display};
    }
};

using Execution = render_execution::RenderExecution<Backend>;


struct DisplayFrame
{
    gpu::SubmittedRGB32fImage image;
    std::shared_ptr<DisplayProgram> program;
    image_processing::OutputTransformSettings settings;
};

std::string failure_text(std::exception_ptr error)
{
    try 
    { 
        if(error){
            std::rethrow_exception(error);
        }  
    }
    catch (const std::exception& e) 
    { 
        return e.what(); 
    }
    catch (...) 
    { 
        return "Unknown worker failure"; 
    }

    return {};
}

viewer::WindowOptions window_options(
    const Application& app,
    const ViewerOptions& options
)
{
    if (!app.create_source) throw std::invalid_argument("A frame-source factory is required");
    image_processing::validate_output_transform_settings(app.display);

    viewer::WindowOptions result;
    result.title = app.title;
    result.visible = options.visible;
    result.vsync = options.vsync;
    result.check_gl_errors = options.validation;
    return result;
}
} // namespace

struct Viewer::Impl
{
    Application application_;
    viewer::Window window_;
    viewer::Workspace workspace_;

    std::unique_ptr<Execution> execution_;
    tasks::WorkerStatus worker_status_;
    std::optional<gpu::SubmittedRGB32fImage> latest_;
    std::shared_ptr<DisplayProgram> display_;

    std::optional<DisplayFrame> presenting_;
    std::optional<DisplayFrame> presented_;
    std::unique_ptr<cuda_gl_display::SurfacePresenter> presenter_;
    image_export::SnapshotWriter snapshot_writer_;
    SnapshotStatus snapshot_status_;
    SnapshotPanel snapshot_panel_;

    std::exception_ptr error_;
    std::string error_message_;
    bool vsync_;
    bool validation_;
    bool render_requested_ = false;
    bool display_dirty_ = false;

    image_processing::OutputTransformSettings display_settings_;
    std::uint64_t received_images_ = 0;
    viewer::WorkspacePanels panels_;

    Impl(Application application, const ViewerOptions& options)
        :   application_(std::move(application)),
            window_(window_options(application_, options)),
            vsync_(options.vsync),
            validation_(options.validation),
            display_settings_(application_.display)
    {
        panels_.draw_render_view = [this] { draw_render_view(); };
        panels_.draw_inspector = [this] { draw_inspector(); };
    }

    void fail(std::exception_ptr error)
    {
        error_ = std::move(error);
        error_message_ = failure_text(error_);
        render_requested_ = false;
    }

    void set_display_settings(const image_processing::OutputTransformSettings& settings)
    {
        image_processing::validate_output_transform_settings(settings);
        if(settings == display_settings_) return;
        display_settings_ = settings;
        display_dirty_ = true;
    }

    void collect_result()
    {
        if (!execution_) return;
        worker_status_ = execution_->status();
        if (worker_status_.error) std::rethrow_exception(worker_status_.error);
        if (worker_status_.phase == tasks::WorkerPhase::stopped)
            throw std::runtime_error("Renderer session stopped");
        if (worker_status_.phase != tasks::WorkerPhase::completed) return;

        auto reply = execution_->try_take_result();
        if (!reply) return;
        latest_.emplace(std::move(reply->image));
        display_ = std::move(reply->display);
        display_dirty_ = true;
        if (received_images_ != (std::numeric_limits<std::uint64_t>::max)()) ++received_images_;
        worker_status_ = execution_->status();
        if (!presenter_)
        {
            const auto view = latest_->image();
            presenter_ = std::make_unique<cuda_gl_display::SurfacePresenter>(
                view.memory.context, latest_->context_lifetime(), view.width, view.height);
        }
    }

    void update_display()
    {
        if (!presenter_) return;
        if(presenter_->poll())
        {

            presented_ = std::move(presenting_);
            presenting_.reset();
        }
        if (!latest_ || !display_dirty_ || presenter_->pending()) return;

        
        presenting_.emplace(DisplayFrame{*latest_, display_, display_settings_});
        const bool accepted = presenter_->try_submit([this]
            (gpu::RGBA8SurfaceView surface, const cdw::Stream& stream)
        {
            const auto& frame = *presenting_;
            cdw::stream_wait_event(stream, frame.image.completion_event());
            frame.program->transform.enqueue(frame.image.image(), surface, stream, frame.settings);
        });
        if (accepted) display_dirty_ = false;
        else presenting_.reset(); // A refusal enqueued nothing; retry with the latest settings.
    }

    void snapshot_failed(std::exception_ptr error)
    {
        snapshot_status_.phase = SnapshotPhase::failed;
        snapshot_status_.message = "Snapshot failed: " + failure_text(error);
    }

    bool try_save_snapshot(std::filesystem::path path)
    {
        if (error_ || !presented_ || snapshot_writer_.busy()) return false;
        try
        {
            SnapshotStatus submitting{SnapshotPhase::saving, path, "Saving snapshot..."};
            const auto& frame = *presented_;
            // The alias owns DisplayProgram (and its context), not just a raw module pointer.
            std::shared_ptr<const image_processing::OutputTransform> transform(
                frame.program, &frame.program->transform);
            if (!snapshot_writer_.try_submit(frame.image, std::move(transform), frame.settings, std::move(path)))
                return false;
            snapshot_status_ = std::move(submitting);
            return true;
        }
        catch (...)
        {
            snapshot_status_.path.clear();
            snapshot_failed(std::current_exception());
            return false;
        }
    }

    void update_snapshot()
    {
        if (!snapshot_writer_.busy()) return;
        try
        {
            if (auto path = snapshot_writer_.poll())
            {
                const auto bytes = path->u8string();
                snapshot_status_ = {SnapshotPhase::saved, std::move(*path),
                    "Saved: " + std::string(bytes.begin(), bytes.end())};
            }
        }
        catch (...) { snapshot_failed(std::current_exception()); }
    }

    void start_execution()
    {
        if (execution_) return;
        // Visible UI exists before OptiX/module/GAS preparation starts.
        const LaunchEnvironment environment{
            cuda_gl_display::device_for_current_gl_context(),
            application_.module_directory, validation_};
        execution_ = std::make_unique<Execution>(
            [factory = std::move(application_.create_source), environment](const tasks::TaskContext& task)
            {
                auto backend = std::make_unique<Backend>();
                task.report("Preparing renderer session...");
                backend->source = factory(environment, task);
                if (!backend->source) throw std::runtime_error("Frame source is null");
                task.report("Loading the shared linear/surface transform module...");
                backend->display = std::make_shared<DisplayProgram>(backend->source->info(),
                    environment.module_directory / "image_processing" / "output_transform.fatbin");
                return backend;
            });
    }

    void submit_request()
    {
        if (!execution_ || !render_requested_ || !worker_status_.accepting()) return;
        if (execution_->try_request())
        {
            render_requested_ = false;
            worker_status_ = execution_->status();
        }
    }

    ViewerStatistics statistics() const noexcept
    {
        return {received_images_, presenter_ ? presenter_->statistics().publications : 0};
    }

    ViewerStatus status() const
    {
        ViewerStatus result;
        result.render_requested = render_requested_;
        result.submission_pending =
            worker_status_.phase == tasks::WorkerPhase::queued
            || worker_status_.phase == tasks::WorkerPhase::running
            || worker_status_.phase == tasks::WorkerPhase::completed;
        result.display_dirty = display_dirty_;
        result.error = error_;

        if (error_)
        {
            result.phase = ViewerPhase::failed;
            result.message = error_message_;
            return result; // A failed presenter rejects further status queries.
        }
        result.display_pending = presenter_ && presenter_->pending();
        result.has_image = presenter_ && presenter_->has_image();
        if (!execution_ || worker_status_.phase == tasks::WorkerPhase::initializing)
        {
            result.phase = ViewerPhase::initializing;
            result.message = execution_ ? worker_status_.message : "Connecting the display GPU...";
        }
        else if (render_requested_ || result.submission_pending)
        {
            result.phase = ViewerPhase::submitting;
            result.message = result.submission_pending ? "Submitting a renderer frame..." : "Waiting for a render slot...";
        }
        else if (result.display_pending || display_dirty_)
        {
            result.phase = ViewerPhase::presenting;
            result.message = "Rendering / updating surface...";
        }
        else
        {
            result.phase = ViewerPhase::idle;
            result.message = "Idle: no renderer/transfer work is issued for UI-only refreshes.";
        }
        return result;

    }

    void draw_render_view()
    {
        if (error_) { ImGui::TextWrapped("%s", error_message_.c_str()); return; }
        if (!presenter_ || !presenter_->has_image())
        { ImGui::TextUnformatted("Preparing the first GPU image..."); return; }
        const auto available = ImGui::GetContentRegionAvail();
        if (available.x <= 0 || available.y <= 0) return;
        const float w = static_cast<float>(presenter_->width());
        const float h = static_cast<float>(presenter_->height());
        const float scale = (std::min)(available.x / w, available.y / h);
        const ImVec2 size(w * scale, h * scale);
        const auto p = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(p.x + (available.x - size.x) * 0.5f,
            p.y + (available.y - size.y) * 0.5f));
        // Row 0 is TOP for the renderer AND this surface. No framebuffer Y flip.
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(presenter_->texture_name())),
            size, ImVec2(0, 0), ImVec2(1, 1));
    }

    void draw_inspector()
    {
        ImGui::TextUnformatted("Persistent session / surface display");
        ImGui::Text("UI: %.1f FPS", ImGui::GetIO().Framerate);
        const auto counts = statistics();
        ImGui::Text("Render submissions received: %llu",
            static_cast<unsigned long long>(counts.received_images));
        ImGui::Text("Display updates: %llu",
            static_cast<unsigned long long>(counts.display_updates));
        if (ImGui::Checkbox("VSync", &vsync_)) window_.set_vsync(vsync_);
        const bool ready = execution_ && worker_status_.accepting() && !error_;
        ImGui::BeginDisabled(!ready || render_requested_);
        if (ImGui::Button("Render again")) render_requested_ = true;
        ImGui::EndDisabled();
        auto settings = display_settings_;
        if (ImGui::SliderFloat("Exposure (EV)", &settings.exposure_ev,
            image_processing::minimum_exposure_ev, image_processing::maximum_exposure_ev))
            set_display_settings(settings);
        ImGui::TextWrapped("Exposure only updates the display surface. Resizing the UI does not render or rebuild the scene.");
        if (error_) ImGui::TextWrapped("%s", error_message_.c_str());
        try
        {
            if (auto path = snapshot_panel_.draw(presented_.has_value() && !error_ && !snapshot_writer_.busy(),
                    snapshot_status_))
                static_cast<void>(try_save_snapshot(std::move(*path)));
        }
        catch (...)
        {
            snapshot_status_.path.clear();
            snapshot_failed(std::current_exception());
        }
        if (application_.draw_inspector) { ImGui::Separator(); application_.draw_inspector(); }
    }

    bool update()
    {
        if (window_.should_close()) return false;
        update_snapshot();
        if (!error_)
        {
            try
            {
                collect_result();
                update_display();
            }
            catch (...) { fail(std::current_exception()); }
        }

        if (!window_.begin_frame()) return !window_.should_close();
        const auto current = status();
        const bool busy = current.phase != ViewerPhase::idle && current.phase != ViewerPhase::failed;
        if (workspace_.draw(panels_, {current.message, busy})) window_.request_close();
        window_.end_frame();

        if (!error_ && !window_.should_close())
        {
            try
            {
                start_execution();
                submit_request();
            }
            catch (...) { fail(std::current_exception()); }
        }
        return !window_.should_close();
    }
};

Viewer::Viewer(Application application, const ViewerOptions& options)
    : impl_(std::make_unique<Impl>(std::move(application), options)) {}
Viewer::~Viewer() noexcept = default;
bool Viewer::update() { return impl_->update(); }
void Viewer::request_render()
{
    if (impl_->error_) std::rethrow_exception(impl_->error_);
    impl_->render_requested_ = true;
}
void Viewer::set_display_settings(const image_processing::OutputTransformSettings& settings)
{ impl_->set_display_settings(settings); }
image_processing::OutputTransformSettings Viewer::display_settings() const
{ return impl_->display_settings_; }
bool Viewer::try_save_snapshot(std::filesystem::path path)
{ return impl_->try_save_snapshot(std::move(path)); }
SnapshotStatus Viewer::snapshot_status() const { return impl_->snapshot_status_; }
void Viewer::request_close() { impl_->window_.request_close(); }
ViewerStatus Viewer::status() const { return impl_->status(); }
ViewerStatistics Viewer::statistics() const noexcept { return impl_->statistics(); }

} // namespace renderer::render_viewer
