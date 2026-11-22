#pragma once
#include <renderer/gpu/submitted_image.hpp>
#include <cuda.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace renderer::triangle
{
// モジュールのルートディレクトリから，セッションが読む OptiX IR のパスを組み立てる．
[[nodiscard]] std::filesystem::path module_path(const std::filesystem::path& module_directory);

struct Settings { std::uint32_t width = 800; std::uint32_t height = 600; };
struct SessionOptions
{
    Settings image{};
    std::uint32_t frame_slots = 2;
    bool optix_validation = false;
    bool diagnose_unwritten_pixels = false;
};
struct SessionStatistics
{
    std::uint64_t pipeline_builds = 0;
    std::uint64_t gas_builds = 0;
    std::uint64_t geometry_uploads = 0;
    std::uint64_t sbt_uploads = 0;
    std::uint64_t parameter_uploads = 0;
    std::uint64_t frame_allocations = 0;
    std::uint64_t render_launches = 0;
    std::uint64_t diagnostic_clears = 0;
};

// 固定シーンの描画セッション．パイプラインとシーンは構築時に一度だけ準備する．
// 描画要求ごとに GAS を再構築したり，バッファを確保・転送したりせず，空きスロットを再利用する．
// 描画の投入には画素の読み戻しや GPU の完了待ちを伴わない．全画面のクリアは診断時に限る．
// セッションの操作は一度に一つの CPU スレッドが担当する．取得した画像ハンドルは
// 別のスレッドに渡してよい．画像を参照する GPU 処理がすべて完了するまで保持する．
class RenderSession final
{
public:
    using Report = std::function<void(std::string)>;
    RenderSession(CUdevice device, const std::filesystem::path& module,
        const SessionOptions& options = {}, Report report = {});
    ~RenderSession() noexcept;
    RenderSession(const RenderSession&) = delete;
    RenderSession& operator=(const RenderSession&) = delete;
    RenderSession(RenderSession&&) = delete;
    RenderSession& operator=(RenderSession&&) = delete;

    // true: 1 回の描画を投入した（GPU 完了ではない）．
    // false: 空きスロットがなく未投入．未取得の画像もそのまま残る．
    [[nodiscard]] bool try_advance();

    // 最新の未取得画像を 1 回だけ返す．描画の投入や GPU の完了待ちは行わない．
    // 取得前に複数回の投入が成功した場合，最後の画像だけを取得できる．
    // セッションを操作する同じ CPU 所有者が取得し，利用者へハンドルを渡す．
    [[nodiscard]] std::optional<gpu::SubmittedRGB32fImage> take_latest_image();
    // セッションの所有者が初期化の完了をポーリングし，完了していれば一時バッファを解放する．
    // 初期化が進行中なら true を返すので，後で再度呼び出す．
    bool collect();
    // セッションが投入した GPU 処理の完了を待つ．バッチ描画の完了時や終了処理で使う．
    void wait_idle();
    [[nodiscard]] CUcontext context() const noexcept;
    [[nodiscard]] std::shared_ptr<const void> context_lifetime() const;
    [[nodiscard]] Settings settings() const noexcept;
    [[nodiscard]] SessionStatistics statistics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
