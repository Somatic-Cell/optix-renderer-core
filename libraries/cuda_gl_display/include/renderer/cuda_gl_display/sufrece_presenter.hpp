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


// 生成した GL スレッドで操作/破棄し，生成時の GL コンテキストを current にしておく
class SurfacePresenter final
{
public:
    // この呼び出し内で，サーフェスを使うすべての処理を指定のストリームに投入する
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

    // 書き込み先が使用中なら false を返す．その場合は producer を呼ばず，再試行できる
    [[nodiscard]] bool try_submit(Producer producer);

    // 新しいテクスチャを公開したときに true を返す．以後の OpenGL 描画から参照できる
    [[nodiscard]] bool poll();
    // 受け付けた画像が，まだ poll() で公開されていない間は true
    [[nodiscard]] bool pending() const;
    [[nodiscard]] bool has_image() const;
    [[nodiscard]] unsigned int texture_name() const;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] PresentationStatistics statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};


} // namespace renderer::cuda_gl_display