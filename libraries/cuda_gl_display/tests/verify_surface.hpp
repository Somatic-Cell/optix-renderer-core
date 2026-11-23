#pragma once

#include <renderer/gpu/image_view.hpp>
#include <renderer/image_processing/output_transform.hpp>

namespace renderer::cuda_gl_display::tests
{
// 呼び出し元が入力の準備完了と，texture の CUDA からの unmap 完了を保証する．
// 読み戻しと参照値の計算はテスト専用であり，通常の表示経路では呼ばない．
void verify_surface(unsigned int texture, gpu::ConstDeviceRGB32fImageView source,
    const image_processing::OutputTransform& transform,
    const image_processing::OutputTransformSettings& settings);
}
