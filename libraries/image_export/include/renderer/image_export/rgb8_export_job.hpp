#pragma once 

#include <renderer/gpu/image_view.hpp>
#include <renderer/image_types/image.hpp>
#include <renderer/image_processing/output_transform_settings.hpp>

#include <cstdint>
#include <memory>

namespace cuda_driver_wrapper { class Stream; class Event; }
namespace renderer::image_processing {class OutputTransform; }

namespace renderer::image_export
{

class RGB8ExportJob final
{
public:
    RGB8ExportJob(
        const image_processing::OutputTransform& transform,
        const cuda_driver_wrapper::Stream& stream,
        std::uint32_t width,
        std::uint32_t height 
    );
    ~RGB8ExportJob() noexcept;

    // copy / move の禁止
    RGB8ExportJob(const RGB8ExportJob&) = delete;
    RGB8ExportJob& operator=(const RGB8ExportJob&) = delete;
    RGB8ExportJob(const RGB8ExportJob&&) = delete;
    RGB8ExportJob& operator=(RGB8ExportJob&&) = delete;

    // 1 回だけよぶ
    // GPU での画像の変換と CPU への転送を依頼する
    // 呼び出した CPU スレッドは GPU の完了を待たない
    void submit(
        gpu::ConstRGB32fImageView input,
        const cuda_driver_wrapper::Event& producer_completion,
        const image_processing::OutputTransformSettings& settings = {}
    );

    // 呼び戻し処理について，現在の状態を返す
    [[nodiscard]] bool is_ready();
    
    // 現在の処理が未完了なら完了するまで待つ
    void wait();

    // 読み戻した CPU メモリを参照するための情報を返す
    // 処理が未完了の場合は取得できない
    [[nodiscard]] RGB8ImageView view();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};


} // renderer::image_export