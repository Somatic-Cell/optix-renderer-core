#include <renderer/output/srgb8_readback.hpp>
#include <renderer/image_processing/output_transform.hpp>

#include <cuda_driver_wrapper/device_buffer.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <cuda_driver_wrapper/pinned_host_buffer.hpp>
#include <cuda_driver_wrapper/stream.hpp>

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace renderer::output
{

namespace 
{

namespace cdw = cuda_driver_wrapper;

CUcontext checked_context(
    const image_processing::OutputTransform& transform,
    const cdw::Stream& stream
)
{
    if(!stream.is_valid() || stream.context() != transform.context())
    {
        throw std::invalid_argument("Readback stream must match output transform context");
    }
    return transform.context();
}

std::size_t checked_image_size(
    std::uint32_t width, 
    std::uint32_t height
)
{
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("Readback image dimensions must be positive");
    }

    return cdw::checked_byte_size(cdw::checked_byte_size(width, height), sizeof(RGB8));
}

} // namespace

struct RGB8Readback::Impl
{
    enum class State {
        idle,           // まだ処理を依頼していない初期状態．readback は 1 回だけ行う
        submitted,      // 処理を GPU に依頼した
        complete,       // 処理が完了した
        failed          // 処理を依頼したが失敗した
    };
    
    const image_processing::OutputTransform& transform;
    const cdw::Stream& stream;
    CUcontext context;
    std::uint32_t width;
    std::uint32_t height;
    std::size_t image_bytes;
    cdw::DeviceBuffer device_pixels;
    cdw::PinnedHostBuffer host_pixels;
    cdw::Event completed;
    State state = State::idle;
    bool pending = false;

    Impl(
        const image_processing::OutputTransform& transform_in,
        const cdw::Stream& stream_in,
        std::uint32_t width_in,
        std::uint32_t height_in
    ) : transform(transform_in), 
        stream(stream_in),
        context(checked_context(transform, stream)),
        width(width_in),
        height(height_in),
        image_bytes(checked_image_size(width, height)),
        device_pixels(context, image_bytes),
        host_pixels(context, image_bytes),
        completed(context)
    {
    }

    ~Impl() noexcept
    {
        if (!pending)
        {
            return;
        }

        try
        {
            //投入の途中で例外になった場合でも，バッファを解放する前に処理の完了を待つ
            cdw::synchronize_stream(stream);
        }
        catch(const std::exception& error)
        {
            std::fprintf(stderr, "[RGB8 readback cleanup] %s\n", error.what());
        }
        catch(...)
        {
            std::fputs("[RGB8 readback cleanup] unknown failure\n", stderr);
        }
        
    }

    // GPU での画像の変換と CPU への転送を依頼する
    // 呼び出した CPU スレッドは GPU の完了を待たない
    void submit(
        gpu::ConstLinearRGBImageView input,     // 呼び戻したい GPU 上の画像
        const cdw::Event& producer_completion,  // 描画の完了を記録するイベント 
        const image_processing::OutputTransformSettings& settings
    )
    {
        // 処理の投入は一度だけ行うので，submit が呼ばれた時点で idle になっていないのは不正
        if(state != State::idle)
        {
            throw std::logic_error("An RGB8 readback request can be submitted only once");
        }

        // 不正な入力を検知
        state = State::failed;
        if(input.width != width || input.height != height)
        {
            throw std::invalid_argument("Readback input dimensions do not match");
        }

        if(!producer_completion.is_valid() || producer_completion.context() != context)
        {
            throw std::invalid_argument("Readback producer event has the wrong context");
        }

        // 設定が正しいかどうかを判断
        image_processing::validate_output_transform_settings(settings);
        
        const gpu::SRGBImageView destination{
            device_pixels.view(),
            width,
            height,
            cdw::checked_byte_size(width, sizeof(RGB8))
        };

        pending = true;

        // 画像のレンダリングが終わるまで待つ
        // 書き戻しはレンダリングが完了してから
        cdw::stream_wait_event(stream, producer_completion);

        // ポストプロセスの処理をストリームに投入
        transform.enqueue(input, destination, stream, settings);
        // 画像の読み戻し処理を投入
        cdw::copy_device_to_host_async(host_pixels.view(), device_pixels.view(), stream);
        // 読み戻しまで終わったかどうかを記録するイベントを記録
        cdw::record_event(completed, stream);

        // 依頼の完了
        state = State::submitted;
        
        // ポストプロセス・画像の読み戻し完了まで待たずに抜ける
        // 完了したかどうかは completed.is_valid() で確認
    }

    // 呼び戻し処理について，現在の状態を返す
    bool is_ready()
    {
        switch(state)
        {
        // まだ呼び戻し処理を依頼していない
        case State::idle:
            return false;
        // 呼び戻し処理は依頼したが失敗した
        case State::failed:
            throw std::logic_error("RGB8 readback submission failed; no image can be published");
        // 呼び戻し処理が終了した
        case State::complete:
            return true;
        // 呼び戻し処理は依頼した
        case State::submitted:
            // 呼び戻し処理が完了していない
            if(!cdw::is_event_ready(completed))
            {
                return false;
            }
            // 完了した
            state = State::complete;
            pending = false;
            return true;
        }

        // 想定外のフラグ
        throw std::logic_error("Invalid RGB8 readback state");
    }

    // 現在の処理が未完了なら完了するまで待つ
    void wait()
    {
        // 待っても無意味な状態
        if (state == State::idle || state == State::failed)
        {
            throw std::logic_error("wait() requires a successfully submitted RGB8 readback");
        }

        // 処理を依頼したが呼び戻しが完了していない
        if (state == State::submitted)
        {
            // 処理の完了を待つ
            try
            {
                cdw::synchronize_event(completed);
            }
            catch(...)
            {
                state = State::failed;
                throw;
            }

            state = State::complete;
            pending = false;
        }
    }

    // 読み戻した CPU メモリを参照するための情報を返す
    // 処理が未完了の場合は取得できない
    SRGB8ImageView view()
    {
        if(!is_ready())
        {
            throw std::logic_error("RGB8 readback is not ready; wait or poll first");
        }

        return {width, height, host_pixels.bytes()};
    } 
};

RGB8Readback::RGB8Readback(
    const image_processing::OutputTransform& transform,
    const cdw::Stream& stream,
    std::uint32_t width, 
    std::uint32_t height
) : impl_(std::make_unique<Impl>(transform, stream, width, height))
{
}

RGB8Readback::~RGB8Readback() noexcept = default;

void RGB8Readback::submit(
    gpu::ConstLinearRGBImageView input,
    const cuda_driver_wrapper::Event& producer_completion,
    const image_processing::OutputTransformSettings& settings
)
{
    impl_->submit(input, producer_completion, settings);
}

bool RGB8Readback::is_ready() { return impl_->is_ready(); }
void RGB8Readback::wait() { impl_->wait(); }
SRGB8ImageView RGB8Readback::view() { return impl_->view(); }

} // namespace renderer::output