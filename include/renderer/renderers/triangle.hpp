#pragma once

#include <renderer/core/image.hpp>

#include <filesystem>
#include <memory>

namespace cuda_driver_wrapper {class Stream; }
namespace renderer::optix_backend {class DeviceContext; }

namespace renderer::triangle
{

struct Settings
{
    unsigned int width = 800;
    unsigned int height = 600;
};

class RenderJob final
{
public:
    RenderJob(
        const optix_backend::DeviceContext& context,
        const cuda_driver_wrapper::Stream& stream,
        const std::filesystem::path& module_path,
        Settings settings = {}
    );

    ~RenderJob() noexcept;

    // コピー，ムーブの禁止
    RenderJob(const RenderJob&) = delete;
    RenderJob& operator=(const RenderJob&) = delete;
    RenderJob(RenderJob &&) = delete;
    RenderJob& operator=(RenderJob &&) = delete;

    // アップロード，GAS のビルド，レンダリング，読み戻しと完了イベントをエンキューする
    // 1 回だけよぶ
    void submit();

    [[nodiscard]] bool is_ready();

    void wait();

    // CPU 上にある画素群を返す
    [[nodiscard]] LinearRGBImage take_image();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}