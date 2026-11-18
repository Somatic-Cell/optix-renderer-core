#pragma once

#include <renderer/task_execution/task_context.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>        // 複数スレッド間の排他的制御
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>       // スレッドの生成，管理
#include <utility>

namespace renderer::task_execution
{
enum class WorkerPhase {
    initializing,   // ワーカースレッドの初期化中
    idle,           // 初期化が終了し，タスクの投入待ち
    queued,         // タスクを受け付けたが，まだ実行していない
    running,        // タスクを実行中
    completed,      // タスクが正常終了した
    cancelled,      // タスクがキャンセルされた
    failed,         // 初期化，実行などで例外が発生した
    stopped         // 初期化のキャンセルによって停止した
};

// ワーカースレッドの状態を，他のスレッドに公開するための構造体
// Result 型の結果を保持できる
template<class Result>
struct WorkerSnapshot
{
    WorkerPhase phase = WorkerPhase::initializing;
    std::uint64_t generation = 0;
    std::string message = "Preparing session ...";
    std::shared_ptr<const Result> result;   // result の型によっては巨大になりうるので，スナップショットのたびにコピーするを避ける
    std::exception_ptr error;   // 発生した例外を保持する (あるスレッドで生じた例外を別のスレッドに公開する)

    // 新しいタスクを受け付けられる状態かどうか
    [[nodiscard]] bool accepting() const noexcept
    {
        return phase == WorkerPhase::idle
            || phase == WorkerPhase::completed
            || phase == WorkerPhase::cancelled;
    }
};

// State: ワーカースレッドで保持，再利用する内部状態
// Result: タスクの結果として外部へ公開する情報
template<class State, class Result>
class SessionWorker final
{
public:
    // State を生成する．生成した State はワーカースレッド上で保持され，再利用される
    using Factory = std::function<std::unique_ptr<State>(const TaskContext&)>;

    // State の参照と TaskContent を受け取り，Result を返す
    // State は参照を受け取るので，再利用する
    using Operation = std::function<Result(State&, const TaskContext&)>;

    // State に対する処理 (GPU 上の処理の完了確認など) を行う
    using Maintenance = std::function<bool(State&)>;

    // スレッドの生成
    explicit SessionWorker(Factory factory, Maintenance maintenance = {})
        : factory_(std::move(factory)), maintenance_(std::move(maintenance))
    {
        // factory が有効であることを確認
        if (!factory_)
        {
            throw std::invalid_argument("Session factory must not be empty");
        }

        // 新しいスレッドを生成
        worker_ = std::jthread(
            [this](std::stop_token stop) 
            {
                run(stop); 
            }
        );
    }

    ~SessionWorker() noexcept
    {
        // 現在のタスクの停止を Operation に対して依頼
        request_stop();

        // ワーカースレッド全体の停止を run() に対して依頼
        worker_.request_stop();

        // // 待機中のスレッドをすべて起動する
        // wake_.notify_all();
        
        if(worker_.joinable())
        {
            worker_.join();
        }
    }

    SessionWorker(const SessionWorker&) = delete;
    SessionWorker& operator=(const SessionWorker&) = delete;
    SessionWorker(SessionWorker&&) = delete;
    SessionWorker& operator=(SessionWorker&&) = delete;

    // メインスレッドなどからワーカースレッドへタスクを渡す
    [[nodiscard]] bool try_submit(Operation operation)
    {
        if(!operation) 
        {
            throw std::invalid_argument("Session operation must not be empty");
        }

        // タスクごとに stop_spoirce を生成する
        Work work {std::move(operation), std::stop_source{}, {}, 0};

        {
            // snapshot_ へのアクセスを保護
            std::lock_guard lock(mutex_);
            
            // これからタスクを受け付けられそうか判定
            if(!snapshot_.accepting())
            {
                return false;
            }

            if(snapshot_.generation == (std::numeric_limits<std::uint64_t>::max)())
            {
                throw std::overflow_error("Request generation exhausted");
            }
            work.generation = ++ snapshot_.generation;

            // 前の結果を work に移す
            work.retired_result = std::move(snapshot_.result);
            
            // 新しく作成したタスクの停止状態を，SessionWorker からも参照できるようにする
            request_stop_ = work.stop;
            
            // 共有の状態を更新
            snapshot_.phase = WorkerPhase::queued;
            snapshot_.message = "Queued";
            snapshot_.error = {};
            
            pending_.emplace(std::move(work));
        }   // mutex はここを抜けると解放される

        // ワーカースレッドの起動
        wake_.notify_one();
        return true;
    }

    // タスクのキャンセルをリクエストする
    void request_stop() noexcept
    {
        std::stop_source stop;
        // mutex ではコピーだけを行い，解放してから停止のリクエストを行う (デッドロック防止)
        {
            std::lock_guard lock(mutex_);
            stop = request_stop_;
        }

        stop.request_stop();
    }

    [[nodiscard]] WorkerSnapshot<Result> snapshot() const{
        std::lock_guard lock(mutex_);
        return snapshot_;
    }
private:
    
    // 1 回のタスクのリクエストに必要な情報
    struct Work
    {
        Operation operation;                            // ワーカースレッドで実行する処理
        std::stop_source stop;                          // タスク専用の停止リクエスト
        std::shared_ptr<const Result> retired_result;   // 以前の結果の寿命を延ばすための所有権
        std::uint64_t generation;                       // タスクのリクエストの識別番号
    };

    // TaskContext の生成
    TaskContext context(std::stop_token stop)
    {
        return TaskContext(stop, [this](std::string message)
        {
            std::lock_guard lock(mutex_);
            snapshot_.message = std::move(message);
        });
    }

    // ワーカースレッド内で発生した例外の記録
    void fail(std::exception_ptr error) noexcept
    {
        std::lock_guard lock(mutex_);
        snapshot_.error = std::move(error);
        snapshot_.phase = WorkerPhase::failed;
    }

    // Work を実行し，結果を WorkSnapshot に反映する
    void execute(State& state, Work& work) noexcept
    {
        // これらの情報を，タスクの実行が終わったあとで snapshot_ に移す
        // タスクの実行中は mutex を保持しない
        std::shared_ptr<const Result> result;   
        auto phase = WorkerPhase::completed;
        std::exception_ptr error;

        // タスクの実行
        try
        {
            // 古い所有権を解放
            work.retired_result.reset();
            const auto task = context(work.stop.get_token());
            // 実行前にキャンセルされていないかどうか確認する
            task.cancellation_point();
            // タスクを実行し，結果を格納
            result = std::make_shared<const Result>(work.operation(state, task));
        }
        // タスクがキャンセルされた場合，エラーとしない (ユーザが意図的に要求する処理のため)
        catch(const TaskCancelled&)
        {
            phase = WorkerPhase::cancelled;
        }
        // 例外処理
        catch(...)
        {
            phase = WorkerPhase::failed;
            error = std::current_exception();
        }

        // 解放
        work.operation = {};
        
        // ロックして snapshot_ に反映
        std::lock_guard lock(mutex_);

        snapshot_.result = std::move(result);
        snapshot_.error = std::move(error);
        snapshot_.phase = phase;
        snapshot_.message = 
            phase == WorkerPhase::completed ? "Submission Ready" 
            : phase == WorkerPhase::cancelled ? "Request cancelled" : "Request failed";

    }

    void run(std::stop_token stop) noexcept
    {
        std::unique_ptr<State> state;
        bool failed = false;

        try
        {
            state = factory_(context(stop));
            if(!state)
            {
                throw std::runtime_error("Session factory returned null");
            }

            std::lock_guard lock(mutex_);
            snapshot_.phase = WorkerPhase::idle;
            snapshot_.message = "Session ready";
        }
        catch(const TaskCancelled&)
        {
            std::lock_guard lock(mutex_);
            snapshot_.phase = WorkerPhase::stopped;
            return;
        }
        catch(...)
        {
            fail(std::current_exception());
            failed = true;
        }

        factory_ = {};

        while (!stop.stop_requested())
        {
            bool poll_again = false;
            if(state && !failed && maintenance_)
            {
                try
                {
                    poll_again = maintenance_(*state);
                }
                catch(...)
                {
                    fail(std::current_exception());
                    failed = true;
                }                
            }

            std::optional<Work> work;

            // ロックをかける
            {
                std::unique_lock lock(mutex_);
                const auto has_work = [&] 
                {
                    return pending_.has_value(); 
                };

                if (poll_again)
                {
                    wake_.wait_for(
                        lock,
                        stop, 
                        std::chrono::milliseconds(2), 
                        has_work
                    );
                }
                else {
                    wake_.wait(lock, stop, has_work);
                }

                if(stop.stop_requested()) break;

                if (pending_)
                {
                    work.emplace(std::move(*pending_));
                    pending_.reset();

                    if(!failed)
                    {
                        snapshot_.phase = WorkerPhase::running;
                    }
                }
            }

            if(work && state && !failed)
            {
                execute(*state, *work);
                failed = snapshot().phase == WorkerPhase::failed;
            }

        }

        state.reset();
        pending_.reset();
    }

    Factory factory_;
    Maintenance maintenance_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    WorkerSnapshot<Result> snapshot_;
    std::optional<Work> pending_;
    std::stop_source request_stop_;
    std::jthread worker_;
};
} // namespace renderer::task_execution
