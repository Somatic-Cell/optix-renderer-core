#pragma once 

#include <renderer/core/image.hpp>
#include <renderer/gpu/image_view.hpp>
#include <renderer/image_processing/output_transform_settings.hpp>

#include <cstdint>
#include <memory>

namespace cuda_driver_wrapper { class Stream; class Event; }
namespace render::image_processing {class OutputTransform; }

namespace renderer::output
{

class SRGBReadBack final
{
public:
    SRGBReadBack(
        const image_processing::OutputTransformSettings& transform,
        const cuda_driver_wrapper::Stream& stream,
        std::uint32_t width,
        std::uint32_t height 
    );
    ~SRGBReadBack() noexcept;

    // copy / move の禁止
    SRGBReadBack(const SRGBReadBack&) = delete;
    SRGBReadBack& operator=(const SRGBReadBack&) = delete;
    SRGBReadBack(const SRGBReadBack&&) = delete;
    SRGBReadBack& operator=(SRGBReadBack&&) = delete;

    // GPU での画像の変換と CPU への転送を依頼する
    // 呼び出した CPU スレッドは GPU の完了を待たない
    void submit(
        gpu::ConstLinearRGBImageView input,
        const cuda_driver_wrapper::Event& producer_completion,
        const image_processing::OutputTransformSettings& settings = {}
    );

    // 現在の状態を返す
    [[nodiscard]] bool is_ready();
    
    // 現在の処理が未完了なら完了するまで待つ
    void wait();

    // 読み戻した CPU メモリを参照するための情報を返す
    // 処理が未完了の場合は取得できない
    [[nodiscard]] SRGB8ImageView view();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};


}