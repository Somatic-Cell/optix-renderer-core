#pragma once 

#include <renderer/gpu/image_view.hpp>
#include <renderer/image_types/image.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>


namespace cuda_driver_wrapper { class Stream; class Event; }


namespace renderer::gpu
{

namespace detail
{
class ImageReadbackStorage final
{
public:
    ImageReadbackStorage(
        const cuda_driver_wrapper::Stream& stream,
        std::uint32_t width, std::uint32_t height,
        std::size_t pixel_size, std::size_t pixel_alignment
    );

    ~ImageReadbackStorage() noexcept;

    // コピー，ムーブの禁止
    ImageReadbackStorage(const ImageReadbackStorage&) = delete;
    ImageReadbackStorage& operator=(const ImageReadbackStorage&) = delete;
    ImageReadbackStorage(ImageReadbackStorage&) = delete;
    ImageReadbackStorage& operator=(ImageReadbackStorage&&) = delete;

    // 1 回だけよぶ
    // GPU での画像の変換と CPU への転送を依頼する
    // 呼び出した CPU スレッドは GPU の完了を待たない
    void submit(
        cuda_driver_wrapper::ConstDeviceMemoryView source,
        std::uint32_t width, 
        std::uint32_t height,
        std::size_t row_image_bites,
        const cuda_driver_wrapper::Event* producer
    );

    // 呼び戻し処理について，現在の状態を返す
    [[nodiscard]] bool is_ready();
    
    // 現在の処理が未完了なら完了するまで待つ
    void wait();

    [[nodiscard]] std::span<const std::byte> bytes();
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace detail

template<class Pixel>
class ImageReadback final
{
public:
    ImageReadback(
        const cuda_driver_wrapper::Stream& stream,
        std::uint32_t width,
        std::uint32_t height 
    ) : storage_(stream, width, height, sizeof(Pixel), alignof(Pixel)) {}
    ~ImageReadback() noexcept = default;

    // copy / move の禁止
    ImageReadback(const ImageReadback&) = delete;
    ImageReadback& operator=(const ImageReadback&) = delete;
    ImageReadback(const ImageReadback&&) = delete;
    ImageReadback& operator=(ImageReadback&&) = delete;

    // 1 回だけよぶ
    // GPU での画像の変換と CPU への転送を依頼する
    // 呼び出した CPU スレッドは GPU の完了を待たない
    void submit(
        ConstImageView<Pixel> input,
        const cuda_driver_wrapper::Event& producer
    )
    {
        storage_.submit(input.memory, input.width, input.height, input.row_stride_bytes, &producer);
    }
    
    void submit(
        ConstImageView<Pixel> input
    )
    {
        storage_.submit(input.memory, input.width, input.height, input.row_stride_bytes, nullptr);
    }

    // 呼び戻し処理について，現在の状態を返す
    [[nodiscard]] bool is_ready() { return storage_.is_ready(); }

    // 現在の処理が未完了なら完了するまで待つ
    void wait() { storage_.wait(); }

    // 読み戻した CPU メモリを参照するための情報を返す
    // 処理が未完了の場合は取得できない
    [[nodiscard]] renderer::ImageView<Pixel> view()
    {
        return {storage_.width(), storage_.height(), storage_.bytes()};
    }

private:
    detail::ImageReadbackStorage storage_;
};

using RGB8Readback = ImageReadback<RGB8>;
using RGB32fReadback = ImageReadback<RGB32f>;

} // namespace renderer::gpu