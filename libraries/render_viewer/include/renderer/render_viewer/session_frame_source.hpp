#pragma once

#include <renderer/render_execution/render_session.hpp>
#include <renderer/render_viewer/application.hpp>

#include <concepts>
#include <optional>
#include <utility>

namespace renderer::render_viewer
{

// 表示には，GPU に投入済みの画像と，表示処理が同じコンテキストを使うための情報が必要
template<class S>
concept ViewerSession = render_execution::RenderSessionFor<S, gpu::SubmittedRGB32fImage> && requires(const S& source)
{
    SourceInfo{source.context(), source.context_lifetime()};
};

template<ViewerSession S>
class SessionFrameSource final : public FrameSource
{
public:
    // セッションを最終的な所有先に直接構築し，移動不能なレンダラも扱えるようにする
    template<class... Args> requires std::constructible_from<S, Args...>
    explicit SessionFrameSource(Args&&... args) : session_(std::forward<Args>(args)...) {}

    [[nodiscard]] SourceInfo info() const override
    {
        return {session_.context(), session_.context_lifetime()};
    }

    [[nodiscard]] bool try_advance() override { return session_.try_advance(); }
    [[nodiscard]] std::optional<gpu::SubmittedRGB32fImage> take_latest_image() override
    {
        return session_.take_latest_image();
    }
    [[nodiscard]] task_execution::MaintenanceStatus collect() override { return session_.collect(); }

private:
    S session_;
};

} // namespace renderer::render_viewer
