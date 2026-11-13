#include <renderer/image_export/rgb8_export_job.hpp>
#include <renderer/gpu/image_readback.hpp>
#include <renderer/image_processing/output_transform.hpp>

#include <cuda_driver_wrapper/device_buffer.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <cuda_driver_wrapper/stream.hpp>

#include <cstdio>
#include <exception>
#include <stdexcept>

namespace renderer::image_export
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
        throw std::invalid_argument("Export stream must match output transform context");
    }
    return transform.context();
}

} // namespace

struct RGB8ExportJob::Impl
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
    std::size_t row_bytes;
    std::size_t image_bytes;
    cdw::DeviceBuffer device_pixels;
    gpu::RGB8Readback readback;
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
        row_bytes(checked_row_bytes(width, sizeof(RGB8))),
        image_bytes(packed_image_bytes(width, height, sizeof(RGB8))),
        device_pixels(context, image_bytes),
        readback(stream_in, width_in, height_in)
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
            std::fprintf(stderr, "[RGB8 export cleanup] %s\n", error.what());
        }
        catch(...)
        {
            std::fputs("[RGB8 export cleanup] unknown failure\n", stderr);
        }
        
    }

    // GPU での画像の変換と CPU への転送を依頼する
    // 呼び出した CPU スレッドは GPU の完了を待たない
    void submit(
        gpu::ConstRGB32fImageView input,     // 呼び戻したい GPU 上の画像
        const cdw::Event& producer_completion,  // 描画の完了を記録するイベント 
        const image_processing::OutputTransformSettings& settings
    )
    {
        // 処理の投入は一度だけ行うので，submit が呼ばれた時点で idle になっていないのは不正
        if(state != State::idle)
        {
            throw std::logic_error("An RGB8 export job can be submitted only once");
        }

        // 不正な入力を検知
        if(input.width != width || input.height != height)
        {
            throw std::invalid_argument("Export image dimensions do not match");
        }

        if(!producer_completion.is_valid() || producer_completion.context() != context)
        {
            throw std::invalid_argument("Export producer event has the wrong context");
        }

        // 設定が正しいかどうかを判断
        image_processing::validate_output_transform_settings(settings);
        
        const gpu::RGB8ImageView destination{
            device_pixels.view(),
            width,
            height,
            row_bytes
        };

        state = State::failed;
        pending = true;

        // 画像のレンダリングが終わるまで待つ
        // 書き戻しはレンダリングが完了してから
        cdw::stream_wait_event(stream, producer_completion);

        // ポストプロセスの処理をストリームに投入
        transform.enqueue(input, destination, stream, settings);
        
        // 画像の読み戻し処理を投入
        readback.submit(destination);

        // 依頼の完了
        state = State::submitted;
        
        // ポストプロセス・画像の読み戻し完了まで待たずに抜ける
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
            throw std::logic_error("Export submission / execution failed");

        // 呼び戻し処理が終了した
        case State::complete:
            return true;
       
        // 呼び戻し処理は依頼した
        case State::submitted:
            // 呼び戻し処理が完了していない
            if(!readback.is_ready())
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
            throw std::logic_error("wait() requires a successfully submitted export job");
        }

        // 既に完了している場合
        if (state == State::complete)
        {
            return;
        }

        // 処理を依頼したが呼び戻しが完了していない
        if (state == State::submitted)
        {
            // 処理の完了を待つ
            try
            {
                readback.wait();
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

RGB8ExportJob::RGB8ExportJob(
    const image_processing::OutputTransform& transform,
    const cdw::Stream& stream,
    std::uint32_t width, 
    std::uint32_t height
) : impl_(std::make_unique<Impl>(transform, stream, width, height))
{
}

RGB8ExportJob::~RGB8ExportJob() noexcept = default;

void RGB8ExportJob::submit(
    gpu::ConstRGB32fImageView image,
    const cuda_driver_wrapper::Event& event,
    const image_processing::OutputTransformSettings& settings
)
{
    impl_->submit(image, event, settings);
}

bool RGB8ExportJob::is_ready() { return impl_->is_ready(); }
void RGB8ExportJob::wait() { impl_->wait(); }
RGB8ImageView RGB8ExportJob::view() 
{ 
    if (!impl_->is_ready())
    {
        throw std::logic_error("Export image is no ready; wait or poll first");
    }
    return impl_->readback.view(); 
}

} // namespace renderer::output