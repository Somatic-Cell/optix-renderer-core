#include <renderer/gpu/image_readback.hpp>
#include <cuda_driver_wrapper/context.hpp>
#include <cuda_driver_wrapper/error.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <cuda_driver_wrapper/pinned_host_buffer.hpp>
#include <cuda_driver_wrapper/stream.hpp>

#include <cstdio>
#include <exception>
#include <stdexcept>

namespace renderer::gpu::detail
{

namespace 
{

namespace cdw = cuda_driver_wrapper;

CUcontext checked_context(
    const cdw::Stream& stream
)
{
    if(!stream.is_valid() || stream.context() == nullptr)
    {
        throw std::invalid_argument("Readback stream must a valid stream and context");
    }
    return stream.context();
}

} // namespace

struct ImageReadbackStorage::Impl
{
    enum class State {
        idle,           // まだ処理を依頼していない初期状態．readback は 1 回だけ行う
        submitted,      // 処理を GPU に依頼した
        complete,       // 処理が完了した
        failed          // 処理を依頼したが失敗した
    };
    const cdw::Stream& stream;
    CUcontext context;
    std::uint32_t width;
    std::uint32_t height;
    std::size_t pixel_size;
    std::size_t pixel_alignment;
    std::size_t row_bytes;
    std::size_t image_bytes;
    cdw::PinnedHostBuffer host_pixels;
    cdw::Event completed;
    State state = State::idle;
    bool pending = false;

    Impl(
        const cdw::Stream& stream_in,
        std::uint32_t width_in,
        std::uint32_t height_in,
        std::size_t element_size,
        std::size_t alignment
    ) : stream(stream_in),
        context(checked_context(stream)),
        width(width_in),
        height(height_in),
        pixel_size(element_size),
        pixel_alignment(alignment),
        row_bytes(checked_row_bytes(width, element_size)),
        image_bytes(packed_image_bytes(width, height, element_size)),
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
            std::fprintf(stderr, "[image readback cleanup] %s\n", error.what());
        }
        catch(...)
        {
            std::fputs("[image readback cleanup] unknown failure\n", stderr);
        }
        
    }

    // GPU での画像の変換と CPU への転送を依頼する
    // 呼び出した CPU スレッドは GPU の完了を待たない
    void submit(
        cdw::ConstDeviceMemoryView source,
        std::uint32_t w,
        std::uint32_t h,
        std::size_t stride,
        const cdw::Event* producer  // 描画の完了を記録するイベント 
    )
    {
        // 処理の投入は一度だけ行うので，submit が呼ばれた時点で idle になっていないのは不正
        if(state != State::idle)
        {
            throw std::logic_error("An Image readback request can be submitted only once");
        }

        // 不正な入力を検知
        if(w != width || h != height)
        {
            throw std::invalid_argument("Readback input dimensions do not match");
        }

        static_cast<void>(validate_image_memory(source, context, w, h, stride, pixel_size, pixel_alignment));

        if(producer && (!producer->is_valid() || producer->context() != context))
        {
            throw std::invalid_argument("Readback producer event has the wrong context");
        }

        // 入力の検証終わり

        
        state = State::failed;
        pending = true;

        if(producer)
        {
            // 画像のレンダリングが終わるまで待つ
            // 書き戻しはレンダリングが完了してから
            cdw::stream_wait_event(stream, *producer);
        }

        if (stride == row_bytes)
        {
            // 画像の読み戻し処理を投入
            cdw::copy_device_to_host_async(host_pixels.view(), cdw::subview(source, 0, image_bytes), stream);
        }
        else 
        {
            CUDA_MEMCPY2D copy {};
            copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
            copy.srcDevice = source.data;
            copy.srcPitch = stride;
            copy.dstMemoryType = CU_MEMORYTYPE_HOST;
            copy.dstHost = host_pixels.view().data;
            copy.dstPitch = row_bytes;
            copy.WidthInBytes = row_bytes;
            copy.Height = height;
            cdw::ScopedCurrentContext current(context);
            cdw::check_cuda(cuMemcpy2DAsync(&copy, stream.handle()), "cuMemcpy2DAsync");
            current.restore();
        }

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
            throw std::logic_error("Image readback submission failed; no image can be published");
        // 呼び戻し処理が終了した
        case State::complete:
            return true;

        // 呼び戻し処理は依頼した
        case State::submitted:
        default:
            try
            {
                // 呼び戻し処理が完了していない
                if (!cdw::is_event_ready(completed))
                {
                    return false;
                }
            }
            catch(...)
            {
                state = State::failed;
                throw;
            }
        
            // 完了した
            state = State::complete;
            pending = false;
            return true;
        }

    }

    // 現在の処理が未完了なら完了するまで待つ
    void wait()
    {
        // 待っても無意味な状態
        if (state == State::idle || state == State::failed)
        {
            throw std::logic_error("wait() requires a successfully submitted readback");
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
};

ImageReadbackStorage::ImageReadbackStorage(
    const cdw::Stream& stream,
    std::uint32_t width, 
    std::uint32_t height,
    std::size_t pixel_size,
    std::size_t alignment
) : impl_(std::make_unique<Impl>(stream, width, height, pixel_size, alignment))
{
}

ImageReadbackStorage::~ImageReadbackStorage() noexcept = default;

void ImageReadbackStorage::submit(
    cdw::ConstDeviceMemoryView source,
    std::uint32_t width, 
    std::uint32_t height,
    std::size_t stride,
    const cdw::Event* producer
)
{
    impl_->submit(source, width, height, stride, producer);
}

bool ImageReadbackStorage::is_ready() { return impl_->is_ready(); }
void ImageReadbackStorage::wait() { impl_->wait(); }

std::span<const std::byte> ImageReadbackStorage::bytes()
{
    if(!impl_->is_ready())
    {
        throw std::logic_error("Readback is not ready; wait or poll first");
    }
    return impl_->host_pixels.bytes();
}

std::uint32_t ImageReadbackStorage::width() const noexcept { return impl_->width; }
std::uint32_t ImageReadbackStorage::height() const noexcept { return impl_->height; }

} // namespace renderer::gpu::detail