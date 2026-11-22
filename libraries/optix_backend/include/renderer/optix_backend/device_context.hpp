#pragma once

#include <cuda.h>
#include <optix.h>

namespace renderer::optix_backend
{

class DeviceContext final
{
public:
    // OptiX のログ出力
    // 0: 無効，3: 警告，4: 進捗を含める
    explicit DeviceContext(CUcontext cuda_context, bool enable_validation = false, int log_level = 4);
    ~DeviceContext() noexcept;

    // コピー，ムーブの禁止
    DeviceContext(const DeviceContext&) = delete;
    DeviceContext& operator=(const DeviceContext&) = delete;
    DeviceContext(DeviceContext&&) = delete;
    DeviceContext& operator=(DeviceContext&&) = delete;

    [[nodiscard]] CUcontext cuda_context() const noexcept {return cuda_context_; }
    [[nodiscard]] OptixDeviceContext handle() const noexcept {return context_; }

private:
    void close_noexcept() noexcept;

    CUcontext cuda_context_ = nullptr;
    OptixDeviceContext context_ = nullptr;
};

} // namespace renderer::optix_backend