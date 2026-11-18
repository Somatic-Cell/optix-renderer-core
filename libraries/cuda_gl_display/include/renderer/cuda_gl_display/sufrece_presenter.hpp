#pragma once

#include <renderer/gpu/surface_view.hpp>
#include <cuda_driver_wrapper/stream.hpp>
#include <cstdint>
#include <functional>
#include <memory>

namespace renderer::cuda_gl_display
{

[[nodiscard]] CUdevice device_for_current_gl_context();

// 対応する動作をカウントする
// 不要なリソースの生成が行われていないかどうかをチェックする
struct PresentationStatistics
{
    std::uint64_t textures = 0;
    std::uint64_t registrations = 0;
    std::uint64_t mappings = 0;
    std::uint64_t surface_objects = 0;
    std::uint64_t productions = 0;
    std::uint64_t publications = 0;
    std::uint64_t retirement_fences = 0;
};


// poll / submit は ImGui の draw list を作る前に呼ぶ
class SurfacePresenter final
{
public:
    // ラムダ式の寿命が先に尽きたとき，無効な参照を使わないようにするための工夫
    using Producer = std::function<void(gpu::RGBA8SurfaceView, const cuda_driver_wrapper::Stream&)>;
    
    SurfacePresenter(
        CUcontext context, 
        std::shared_ptr<const void> context_lifetime, 
        std::uint32_t width, 
        std::uint32_t height
    );

    ~SurfacePresenter() noexcept;

    // copy move の禁止
    SurfacePresenter(const SurfacePresenter&) = delete;
    SurfacePresenter& operator=(const SurfacePresenter&) = delete;
    SurfacePresenter(SurfacePresenter&&) = delete;
    SurfacePresenter& operator=(SurfacePresenter&&) = delete;

    // bool が返ってきた: submit できていない
    // もう一度 try すること
    [[nodiscard]] bool try_submit(Producer producer);

    [[nodiscard]] bool poll();
    [[nodiscard]] bool pending() const;
    [[nodiscard]] bool has_image() const;
    [[nodiscard]] unsigned int texture_name() const;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] PresentationStatistics statistics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};


} // namespace renderer::cuda_gl_display