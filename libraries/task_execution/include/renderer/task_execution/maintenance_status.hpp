#pragma once

namespace renderer::task_execution
{

// 回収処理を一回行った後，ワーカが次にいつ確認をするのか
enum class MaintenanceStatus
{
    idle,   // 次のジョブが来るまで待って良い
    pending // 新しいジョブが来なくても，時間をおいて再確認する
};

} // namespace renderer::task_execution