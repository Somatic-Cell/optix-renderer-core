// マルチスレッドのタスク管理
// TaskContext 自身は thread をもたない

#pragma once

#include <exception>
#include <functional>
#include <stop_token>   // C++ 20 で導入, スレッドの処理に停止のリクエストを行う
#include <string>
#include <utility>

namespace renderer::task_execution
{
// タスクがキャンセルされたことを表す独自の例外
// std::exception を継承
class TaskCancelled final : public std::exception
{
public:
    [[nodiscard]] const char* what() const noexcept override { return "Task cancelled"; }
};

// GUI やスレッドの executer に依存しない
// 呼び出し側がメインスレッドで実行するか，ワーカースレッドで実行するかを知らなくていい
// Cooperative cancellation (処理自身が停止のリクエストを確認し，自発的に終了する)
class TaskContext final
{
public:
    // ラムダ式を格納
    using Report = std::function<void(std::string)>;


    explicit TaskContext(
        std::stop_token stop = {},      // 停止のリクエストを受け取るオブジェクト
        Report report = {}              // 
    ) 
    : 
    stop_(stop), 
    report_(std::move(report)) // std::function は呼び出し可能なオブジェクトを保持するため，動的メモリ確保する可能性があるので避ける
    {}

    // 停止のリクエストが発生しているかどうかを bool で返す
    // 処理を中断するかどうかは呼び出し側で判断する
    [[nodiscard]] bool stop_requested() const noexcept
    {
        return stop_.stop_requested();
    }

    // 停止のリクエストが発生していれば例外を投げる 
    // (処理を中断する)
    void cancellation_point() const 
    {
        if(stop_requested())
        {
            throw TaskCancelled{};
        }
    }

    // タスクの進捗の通知
    void report(std::string message) const 
    {
        // 通知の直前にキャンセルのリクエストがあるかどうかを確認
        cancellation_point();

        if(report_)
        {
            report_(std::move(message)); // コピーを減らしたいのでムーブ
        }
    }
private:
    std::stop_token stop_;
    Report report_;
};

} // namespace renderer::task_execution