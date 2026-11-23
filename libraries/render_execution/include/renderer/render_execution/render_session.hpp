#pragma once

#include <renderer/task_execution/maintenance_status.hpp>

#include <concepts>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace renderer::render_execution
{

// ワーカーは結果の取り出しと終了処理で画像を move する
// 例外は送出しない
template<class Session, class Image>
concept RenderSessionFor = std::move_constructible<Image> 
    && std::is_nothrow_move_constructible_v<Image>
    && requires(Session& session, std::stop_token stop)
{
    // 1回分の処理を投入できれば true, 空きがなければ false を返し，呼び出し側が再試行する
    { session.try_advance() } -> std::same_as<bool>;

    // 空きがないとき，ワーカーはここで待ち，停止要求がなければ，try_advance() を再試行
    { session.wait_for_frame_slot(stop) } -> std::same_as<void>;

    // 最後に投入した画像を一度だけ取り出す．未取得の画像がなければ std::nullopt を返す
    { session.take_latest_image() } -> std::same_as<std::optional<Image>>;

    // 完了済みの処理に伴うリソースを回収する．再度確認する必要があれば true を返す
    { session.collect() } -> std::same_as<task_execution::MaintenanceStatus>;
};

// 実行中はセッションから画像の型を推論する
template<class Session>
concept RenderSession = RenderSessionFor<
    Session, typename decltype(std::declval<Session&>().take_latest_image())::value_type
>;

} // namespace renderer::render_execution