#include <renderer/cuda_gl_display/sufrece_presenter.hpp>
#include "gl_functions.hpp"

#include <cudaGL.h>
#include <cuda_driver_wrapper/context.hpp>
#include <cuda_driver_wrapper/device.hpp>
#include <cuda_driver_wrapper/error.hpp>
#include <cuda_driver_wrapper/event.hpp>

#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

namespace renderer::cuda_gl_display
{
namespace cdw = cuda_driver_wrapper;
namespace
{

void count_event(std::uint64_t& total) noexcept
{
    if(total != (std::numeric_limits<std::uint64_t>::max)()) ++total;
}

// OpenGL のコンテキストが存在することを確認
GLFWwindow* require_gl()
{
    // 現在の CPU スレッドで current になっている OpenGL コンテキストに対応する GLFW window を返す
    auto* window = glfwGetCurrentContext();
    if(!window)
    {
        throw std::logic_error("A current GLFW OpenGL context is required");
    }
    return window;
}
} // namespace

// OpenGL に対応する CUDA デバイスを取得する
CUdevice device_for_current_gl_context()
{
    static_cast<void>(require_gl());
    
    // CUDA Driver API を初期化する
    // Memo: レンダラ側で順番を整理すれば不要？
    cdw::initialize_driver();

    
    std::array<CUdevice, 16> devices{}; // CUDA デバイス ID を格納する配列
    unsigned int count = 0;             
    
    // 使用できる CUDA デバイスを最大 16 個取得
    cdw::check_cuda(
        cuGLGetDevices(
            &count,                                     // デバイス数の書き込み先
            devices.data(),                             // デバイス ID の書き込み先
            static_cast<unsigned int>(devices.size()),  // 配列に格納できるデバイス数
            CU_GL_DEVICE_LIST_ALL                       // OpenGL コンテキストが使用する全 GPU を問い合わせる
        ), 
        "cuGLGetDevices"
    );

    // GPU が 1 枚の場合だけ受け入れる
    if(count != 1)
    {
        throw std::runtime_error("A single CUDA-capable OpenGL GPU is required");
    }
    return devices[0];
}

struct SurfacePresenter::Impl
{
    // OpenGL のテクスチャと CUDA のペアを扱うのに必要なリソース 
    struct Slot
    {
        GLuint texture = 0;                     // OpenGL のテクスチャ ID
        CUgraphicsResource resource = nullptr;  // 登録済のリソースのハンドル
        CUsurfObject surface = 0;               // CUDA surface object のハンドル
        detail::Sync retired = nullptr;         // OpenGL の同期オブジェクトを保持
        bool mapped = false;                    // CUDA にマッピングされていえるかどうか
    };

    // 非同期処理の状態
    enum class Phase
    {
        idle,       // 画像の生成に関する要求を処理していない 
        producing,  // CUDA が画像を生成中 or 完了待ち
        releasing   // CUDA のマッピング解除を投入済み，完了待ち
    };

    GLFWwindow* window_ = require_gl();
    std::thread::id thread_ = std::this_thread::get_id();   // 構築時の CPU スレッド
    detail::GLFunctions gl_;                                // OpenGL の関数を実行時にロードするための構造体

    std::shared_ptr<const void> context_lifetime_;
    CUcontext context_;
    std::uint32_t width_;
    std::uint32_t height_;
    cdw::Stream stream_;
    cdw::Event produced_;
    cdw::Event released_;
    std::array<Slot, 2> slots_{};
    Producer producer_;
    PresentationStatistics statistics_{};
    
    // -1: 該当なし, 0: 0 番目の slot, 1: 1 番目のスロット
    int front_      = -1;   // 現在表示しているテクスチャのインデックス
    int pending_    = -1;   // CUDA が画像を生成しているテクスチャのインデックス
    
    Phase phase_ = Phase::idle;
    
    bool failed_ = false;

    Impl(
        CUcontext context, 
        std::shared_ptr<const void> lifetime,
        std::uint32_t width,
        std::uint32_t height
    ) : 
    context_lifetime_(std::move(lifetime)),
    context_(context),
    width_(width),
    height_(height),
    stream_(context),
    produced_(context),
    released_(context)
    {
        if(!context_lifetime_ || width == 0 || height == 0)
        {
            throw std::invalid_argument("Surface presenter requrires an owned context and positive dimensions");
        }

        try
        {
            // OpenGL がサポートする最大のテクスチャサイズを取得
            GLint maximum = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);

            // サポートされないサイズのテクスチャは生成しない
            if(
                maximum <= 0 
                || width > static_cast<std::uint32_t>(maximum)
                || height > static_cast<std::uint32_t>(maximum)
                || width > static_cast<std::uint32_t>((std::numeric_limits<int>::max)() / 4)
            )
            {
                throw std::invalid_argument("Display dimensions exceed texture/surface limits");
            }

            cdw::ScopedCurrentContext current(context_);
            
            // CUDA と OpenGL が同じ GPU を使用していることをチェック
            CUdevice device = 0;
            cdw::check_cuda(cuCtxGetDevice(&device), "cCtxGetDevice");

            if(device != device_for_current_gl_context())
            {
                throw std::invalid_argument("CUDA and OpenGL must use the same device");
            }

            // OpenGL テクスチャの生成と CUDA への登録
            for (auto& slot: slots_)
            {
                // 2D テクスチャを生成
                gl_.create_textures(GL_TEXTURE_2D, 1, &slot.texture);   
                // ミップレベル 1 ぶんのテクスチャを確保
                gl_.texture_storage(slot.texture, 1, detail::rgba8, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
                
                // 線形補間する
                gl_.texture_parameter(slot.texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                gl_.texture_parameter(slot.texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                
                // テクスチャの座標が画像の範囲を外れた場合，端の画素を使用する
                gl_.texture_parameter(slot.texture, GL_TEXTURE_WRAP_S, static_cast<GLint>(detail::clamp_to_edge));
                gl_.texture_parameter(slot.texture, GL_TEXTURE_WRAP_T, static_cast<GLint>(detail::clamp_to_edge));
                
                detail::check_gl("Create presentation texture");
                count_event(statistics_.textures);
                
                // OpenGL テクスチャを CUDA に登録する
                // 登録は初期化時に行い，マッピングを画像生成ごとに行う
                cdw::check_cuda(
                    cuGraphicsGLRegisterImage(
                        &slot.resource, slot.texture, GL_TEXTURE_2D,
                        static_cast<unsigned int>(CU_GRAPHICS_REGISTER_FLAGS_SURFACE_LDST)
                        | static_cast<unsigned int>(CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD)
                    ),
                    "cuGraphicsGLRegisterImage"
                );

                count_event(statistics_.registrations);
            }

            current.restore();
        }
        catch (...)
        {
            // コンストラクタの内部で例外が発生した場合はデストラクタが呼ばれないので，明示的に開放
            cleanup();
            throw;
        }
    }


    ~Impl() noexcept { cleanup(); }

    // スレッド ID と捜査対象の OpenGL のコンテキストがそれぞれ一致することを確認
    void require_owner() const 
    {
        if(thread_ != std::this_thread::get_id() || glfwGetCurrentContext() != window_)
        {
            throw std::logic_error("SurfacePresenter requires its creating GL thread / context");
        }

        // 何らかの CUDA-OpenGL の処理が途中で失敗している場合
        // 処理を続けることを禁止し，失敗したオブジェクトを破棄して作り直す
        if(failed_)
        {
            throw std::logic_error("SurfacePresenter failed; reconstruct before use");
        }
    }

    // OpenGL がテクスチャを使い終えたかどうかを確認する
    // 指定された slot が CUDA の書き込み先として再利用できるかどうかを確認
    bool available(Slot& slot)
    {
        // 再利用可能かどうか
        if(!slot.retired)
        {
            return true;
        }

        // glClientWaitAsync() を呼ぶ
        const auto status = gl_.poll(
            slot.retired,   // 確認するフェンス
            0,              // 特別なフラッシュ動作を指定しない
            0               // 完了していなくても待機しない
        );
        // GPU がまだ描画処理を完了していなければ false 
        // フェンスは削除しないので再確認可能
        if(status == detail::timeout_expired) return false;
        
        // 異常な完了状態
        if(status != detail::already_signaled && status != detail::condition_satisfied)
        {
            throw std::runtime_error("glClientWaitSync failed");
        }

        // 役割を終えた同期オブジェクトを破棄
        // フェンスを削除しただけでテクスチャの削除はしない
        // テクスチャは次の CUDA 書き込みに再利用
        gl_.delete_sync(slot.retired);
        slot.retired = nullptr;
        return true;
    }

    // フェンスを新規作成する
    void retire_front()
    {
        // Front としての役割が終わっていないならフェンスを作らない
        if (front_ < 0) return;

        auto& slot = slots_[static_cast<std::size_t>(front_)];

        if(slot.retired) throw std::logic_error("Displayed slot cannot already be retired");
        
        slot.retired = gl_.fence(
            detail::sync_complete, // GL_SYNC_GPU_COMMANDS_COMPLETE
            0
        );
        if (!slot.retired) throw std::runtime_error("glFenceSync failed");
        glFlush();
        count_event(statistics_.retirement_fences);
    }

    // CUDA へ生成した画像を投入する
    bool try_submit(Producer producer)
    {
        require_owner();
        if(!producer) throw std::invalid_argument("Surface producer must not be empty");
        
        // 画像の生成処理が進行中のスロットが 0 以上: 既に生成処理が投入されいている
        if (pending_ >= 0) return false;

        // 現在表示に使っているテクスチャとは反対側のスロットを選択
        const int index = front_ == 0 ? 1 : 0;
        auto& slot = slots_[static_cast<std::size_t>(index)];
        
        // OpenGL が使用中の場合，処理を投入せずに戻る
        if(!available(slot)) return false;

        // GPU の処理が終わるまで保持する
        producer_ = std::move(producer);
        
        // 投入後にエラーが検出される可能性があるため，先にリソースを使用中であることを記録
        pending_ = index;           
        phase_= Phase::producing;
        
        try
        {
            cdw::ScopedCurrentContext current(context_);
            // OpenGL リソースを CUDA からアクセス可能な状態へ遷移（マッピング）する
            cdw::check_cuda(
                cuGraphicsMapResources(
                    1,              // マッピングするリソース数 
                    &slot.resource, // 対象のリソースのポインタ
                    stream_.handle() // 操作を同期するための CUDA Stream
                ),
                "cuGraphicsMapResources"
            );
            slot.mapped = true;
            count_event(statistics_.mappings);

            // OpenGL のリソースに対応する CUDA Array を取得
            // マッピングのたびにリソースを取得し直す必要がある
            // https://docs.nvidia.com/cuda/cuda-driver-api/cuda_driver_api/group__CUDA__GRAPHICS.html
            CUarray array = nullptr;
            cdw::check_cuda(
                cuGraphicsSubResourceGetMappedArray(
                    &array, slot.resource, 0, 0
                ), 
                "cuGraphicsSubResourceGetMappedArray"
            );

            CUDA_RESOURCE_DESC descriptor{};
            descriptor.resType = CU_RESOURCE_TYPE_ARRAY;
            descriptor.res.array.hArray = array;
            
            // surface object を生成
            cdw::check_cuda(
                cuSurfObjectCreate(&slot.surface, &descriptor),
                "cuSurfObjectCreate"
            );
            count_event(statistics_.surface_objects);

            // アプリケーション側から渡された処理を実行
            producer_({context_, slot.surface, width_, height_}, stream_);
            count_event(statistics_.productions);

            // イベントを記録
            cdw::record_event(produced_, stream_);

            current.restore();
            return true;

            // 非同期処理のため，ここでは unmap しない
        }
        catch(...)
        {
            // 失敗した場合はこのフラグが立ち，次回以降の呼び出しで require_owner() が失敗する
            failed_ = true;
            throw;
        }
    }

    // idle: なにもしない
    // producing かつ GPU 未完了: produced_ イベントの完了を確認
    // producing かつ GPU 完了: Unmap を発行し，releasing に進む
    // releasing かつ Unmap 未完了: released_ イベントの完了を確認
    // releasing かつ Unmap 完了: Front を好感して公開
    bool poll()
    {
        // 処理中の画像が存在するかどうかを確認する
        // 無駄な CUDA API の呼び出しを避ける
        require_owner();
        if(pending_ < 0) return false;

        try
        {
            auto& slot = slots_[static_cast<std::size_t>(pending_)];
            
            // 画像の生成の確認
            if(phase_ == Phase::producing)
            {
                // 完了していないならば，GPU の処理が終わるまで CPU を待機させない
                if(!cdw::is_event_ready(produced_)) return false;

                // 完了して入れば，surface object を破棄する処理に進んでよい
                cdw::ScopedCurrentContext current(context_);

                // surface object の破棄
                cdw::check_cuda(
                    cuSurfObjectDestroy(slot.surface),
                    "cuSurfaceObjectDestroy"
                );
                slot.surface = 0;

                // マッピングを解除 (CUDA から OpenGL を触れないようにする)
                cdw::check_cuda(
                    cuGraphicsUnmapResources(
                        1,
                        &slot.resource,
                        stream_.handle()
                    ),
                    "cuGraphicsUnmapResources"
                );

                // アンマップの要求は完了したので CPU 側はフラグを下す
                slot.mapped = false;
                
                // Unmap の完了を確認するイベントを作成
                cdw::record_event(released_, stream_);
                phase_ = Phase::releasing;

                current.restore();
            }

            // Unmap がまだ終わっていないなら終了
            if(!cdw::is_event_ready(released_))
            {
                return false;
            }

            // 現在の front テクスチャに OpenGL フェンスを作成
            retire_front();

            // front と back の交換
            // front だったものには fence がかけられているので，CUDA の書き込み先とする前に
            // 完了確認が行われる

            front_ = pending_;      // pending だったものを，今度は OpenGL の front として使う
            pending_ = -1;          // 
            phase_ = Phase::idle;

            // 保持していた CUDA モジュールをリリースする
            producer_ = {};

            // 公開の完了を報告
            count_event(statistics_.publications);
            return true;
        }
        catch(...)
        {
            failed_ = true;
            throw;
        }
    }

    // リソースの解放
    // GPU がそれぞれのリソースを使い終えていることを保証しつつ，順番にリリースする
    void cleanup() noexcept
    {
        // 破棄するスレッドの検証
        if(thread_ != std::this_thread::get_id() || glfwGetCurrentContext() != window_)
        {
            std::fputs("Destroy SurfacePresenter on its GL thread before Window\n", stderr);
            std::terminate();
        }

        try
        {
            // CUDA Stream の完了を待つ
            cdw::ScopedCurrentContext current(context_);
            if(pending_ >= 0) cdw::synchronize_stream(stream_);

            // CUDA surface object とマッピングを解放
            for(auto& slot : slots_)
            {
                // CUDA surface object の破棄 
                if(slot.surface)
                {
                    cdw::check_cuda(
                        cuSurfObjectDestroy(slot.surface), 
                        "cuSurfObjectDestroy (cleanup)"
                    );
                    slot.surface = 0;
                }

                // アンマップ
                if (slot.mapped)
                {
                    cdw::check_cuda(
                        cuGraphicsUnmapResources(1, &slot.resource, stream_.handle()),
                        "cuGraphicsUnmapResources (cleanup)"
                    );
                    slot.mapped = false;
                    // アンマップの処理も確認したいので，もう一度同期
                    cdw::synchronize_stream(stream_);
                }
            }

            // 現在の OpenGL 側で発行済みの処理が終わるのを待つ
            glFinish();


            for(auto& slot : slots_)
            {
                // CUDA と相互運用するリソースとしての登録そのものを解除する
                if(slot.resource)
                {
                    const auto result = cuGraphicsUnregisterResource(slot.resource);
                    
                    // 解除成功
                    if(result == CUDA_SUCCESS)
                    {
                        slot.resource = nullptr;
                    }
                    // 解除失敗
                    else 
                    {
                        // CUDA への登録が残っている可能性のある OpenGL テクスチャは無理に削除しない
                        // リソースのリークを許容する
                        std::fprintf(stderr, "[surface cleanup] unregister failed: %d\n", static_cast<int>(result));
                    }
                }
            }
            current.restore();
            
        }
        // 例外を外に伝播させない
        catch(const std::exception& error)
        {
            std::fprintf(stderr, "[surface cleanup] %s\n", error.what());
        }
        
        catch(...)
        {
            std::fprintf(stderr, "[surface cleanup] unknown failure");
        }

        for (auto& slot : slots_)
        {
            // フェンスの削除
            if(slot.retired)
            {
                gl_.delete_sync(slot.retired);
            }
            // OpenGL テクスチャの削除
            if(slot.texture && !slot.resource)
            {
                glDeleteTextures(1, &slot.texture);
            }
        }

        // TODO: リソースの解放に失敗しても開放してしまうため，Producer だけが持っていたリソースの寿命の管理方法に改善の余地あり
        producer_ = {};
    }
};

SurfacePresenter::SurfacePresenter(
    CUcontext context, 
    std::shared_ptr<const void> lifetime,
    std::uint32_t width,
    std::uint32_t height
) : impl_(std::make_unique<Impl>(context, std::move(lifetime), width, height)) {}

SurfacePresenter::~SurfacePresenter() noexcept = default;

bool SurfacePresenter::try_submit(Producer producer)
{
    return impl_->try_submit(std::move(producer));
}

bool SurfacePresenter::poll()
{
    return impl_->poll();
}

bool SurfacePresenter::pending() const 
{
    impl_->require_owner();
    return impl_->pending_ >= 0;
}

bool SurfacePresenter::has_image() const 
{
    impl_->require_owner();
    return impl_->front_ >= 0;
}

unsigned int SurfacePresenter::texture_name() const
{
    impl_->require_owner();
    if(impl_->front_ < 0)
    {
        throw std::logic_error("No published surface texture");
    }
    return impl_->slots_[static_cast<std::size_t>(impl_->front_)].texture;
}

std::uint32_t SurfacePresenter::width() const noexcept {return impl_->width_; }
std::uint32_t SurfacePresenter::height() const noexcept {return impl_->height_; }
PresentationStatistics SurfacePresenter::statistics() const noexcept {return impl_->statistics_; }

} // namespace renderer::cuda_gl_display

