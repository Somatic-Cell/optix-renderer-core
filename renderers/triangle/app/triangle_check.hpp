#pragma once

#include <renderer/image_types/image.hpp>
#include <renderer/gpu/image_view.hpp>
#include <renderer/image_processing/output_transform_settings.hpp>

namespace renderer::triangle
{

// 800 × 600 の固定シーンを 8 点で検証する．同期読み戻しを行うため，呼び出し前に描画の完了を待つ．
void verify_triangle(
    gpu::ConstDeviceRGB32fImageView image,
    HostRGB8ImageView  readback,
    const image_processing::OutputTransformSettings& settings
);

} // namespace renderer::triangle
