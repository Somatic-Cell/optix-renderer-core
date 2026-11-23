#pragma once

#include <renderer/task_execution/maintenance_status.hpp>
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
#include <stop_token>
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

struct WorkerStatus
{
    WorkerPhase phase = WorkerPhase::initializing;
    std::string message = "Preparing session ...";
    std::exception_ptr error;   // 発生した例外を保持する (あるスレッドで生じた例外を別のスレッドに公開する)

    // 新しいタスクを受け付けられる状態かどうか
    [[nodiscard]] bool accepting() const noexcept
    {
        return phase == WorkerPhase::idle
            || phase == WorkerPhase::cancelled;
    }
};

// State: ワーカースレッドで保持，再利用する内部状態
// Result: タスクの結果として外部へ公開する情報
// State の構築，操作，破棄はワーカースレッドが担当する
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
    using Maintenance = std::function<MaintenanceStatus(State&)>;

    // スレッドの生成
    explicit SessionWorker(
        Factory factory, 
        Maintenance maintenance = {}, 
        Operation initial = {}
    )
    {
        // factory が有効であることを確認
        if (!factory)
        {
            throw std::invalid_argument("Session factory must not be empty");
        }

        if(initial)
        {
            queue(std::move(initial));
        }

        // 新しいスレッドを生成
        worker_ = std::jthread(
            [this, create = std::move(factory), maintain = std::move(maintenance)](std::stop_token stop) mutable
            {
                run(stop, move(create), maintain); 
            }
        );
    }

    ~SessionWorker() noexcept
    {
        // ワーカースレッド全体の停止を run() に対して依頼
        worker_.request_stop();
        request_stop();
        
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


        {
            // snapshot_ へのアクセスを保護
            std::lock_guard lock(mutex_);
            
            // これからタスクを受け付けられそうか判定
            if(phase_ != WorkerPhase::idle && phase_ != WorkerPhase::cancelled)
            {
                return false;
            }

            queue(std::move(operation));
            set_phase(WorkerPhase::queued);

        }   // mutex はここを抜けると解放される

        // ワーカースレッドの起動
        wake_.notify_one();
        return true;
    }

    // タスクのキャンセルをリクエストする
    void request_stop() noexcept
    {
        std::stop_source stop(std::nostopstate);

        // mutex ではコピーだけを行い，解放してから停止のリクエストを行う (デッドロック防止)
        {
            std::lock_guard lock(mutex_);
            stop = operation_stop_;
        }

        stop.request_stop();
    }

    [[nodiscard]] WorkerStatus status() const {
        std::lock_guard lock(mutex_);
        return {phase_, progress_.empty() ? default_message(phase_) : progress_, error_};
    }

    [[nodiscard]] std::optional<Result> try_take_result()
    {
        std::lock_guard lock(mutex_);
        if(error_)
        {
            std::rethrow_exception(error_);
        }
        if(phase_ != WorkerPhase::completed)
        {
            return std::nullopt;
        }

        return take_locked();
    }

    [[nodiscard]] Result wait_take_result()
    {
        std::unique_lock lock(mutex_);
        if (phase_ == WorkerPhase::idle || (phase_ == WorkerPhase::initializing && !pending_))
        {
            throw std::logic_error("No scheduled session operation");
        }

        result_ready_.wait(lock, [this]{
            return phase_ == WorkerPhase::completed || phase_ == WorkerPhase::failed
                || phase_ == WorkerPhase::cancelled || phase_ == WorkerPhase::stopped;
        });
        if(error_) {
            std::rethrow_exception(error_);
        }
        if(phase_ != WorkerPhase::completed){
            throw TaskCancelled{};
        }
        return take_locked();
    }
private:
    
    // 1 回のタスクのリクエストに必要な情報
    struct Work
    {
        Operation operation;                            // ワーカースレッドで実行する処理
        std::stop_token stop;                           // タスク専用の停止リクエスト
    };

    void queue(Operation operation)
    {
        std::stop_source source;
        pending_.emplace(Work{std::move(operation), source.get_token()});
        operation_stop_ = std::move(source);
    }

    Result take_locked() noexcept
    {
        Result result(std::move(*result_));
        result_.reset();
        set_phase(WorkerPhase::idle);
        return result;
    }

    static const char* default_message(WorkerPhase phase) noexcept
    {
        switch(phase) {
        case WorkerPhase::initializing: return "Prepareing sessin...";
        case WorkerPhase::idle:         return "Session only";
        case WorkerPhase::queued:       return "Queued";
        case WorkerPhase::running:      return "Running";
        case WorkerPhase::completed:    return "Submission ready";
        case WorkerPhase::cancelled:    return "Request cancelled";
        case WorkerPhase::failed:       return "Session failed";
        case WorkerPhase::stopped:      return "Session stopped";
        }
        return "";
    }

    void set_phase(WorkerPhase phase) noexcept
    {
        phase_ = phase;
        progress_.clear();
    }

    // TaskContext の生成
    TaskContext context(std::stop_token stop)
    {
        return TaskContext(stop, [this](std::string message)
        {
            std::lock_guard lock(mutex_);
            progress_= std::move(message);
        });
    }


    // Work を実行し，結果を WorkSnapshot に反映する
    void execute(State& state, Work& work)
    {
        try
        {
            auto task = context(work.stop);
            // 実行前にキャンセルされていないかどうか確認する
            task.cancellation_point();
            // タスクを実行し，結果を格納
            auto result = work.operation(state, task);
            {
                std::lock_guard lock(mutex_);
                result_.emplace(std::move(result));
                operation_stop_ = std::stop_source(std::nostopstate);
                set_phase(WorkerPhase::completed);
            }
        }

        // タスクがキャンセルされた場合，エラーとしない (ユーザが意図的に要求する処理のため)
        catch(const TaskCancelled&)
        {
            std::lock_guard lock(mutex_);
            operation_stop_ = std::stop_source(std::nostopstate);
            set_phase(WorkerPhase::cancelled);
        }
        result_ready_.notify_all();

    }

    void finish(WorkerPhase phase, std::exception_ptr error = {}) noexcept
    {
        std::optional<Work> discarded_work;
        std::optional<Result> discarded_result;
        {
            std::lock_guard lock(mutex_);
            if (pending_)
            {
                discarded_work.emplace(std::move(*pending_));
            }

            if(result_)
            {
                discarded_result.emplace(std::move(*result_));
            }

            pending_.reset();
            result_.reset();

            error_ = std::move(error);

            operation_stop_ = std::stop_source(std::nostopstate);
            set_phase(phase);
        }
        result_ready_.notify_all();
    }

    void run(
        std::stop_token stop,
        Factory factory,
        const Maintenance& maintenance
    ) noexcept
    {
        std::unique_ptr<State> state;
        try
        {
            state = factory(context(stop));
            if(!state)
            {
                throw std::runtime_error("Session factory returned null");
            }

            {
                std::lock_guard lock(mutex_);
                set_phase(pending_ ? WorkerPhase::queued : WorkerPhase::idle);
            }

            while (!stop.stop_requested())
            {
                const auto maintenance_status = maintenance ? maintenance(*state) : MaintenanceStatus::idle;
                std::optional<Work> work;

                {
                    std::unique_lock lock(mutex_);
                    const auto has_work = [this] { return pending_.has_value(); };
                    if (maintenance_status == MaintenanceStatus::pending)
                    {
                        wake_.wait_for(lock, stop, std::chrono::microseconds(2), has_work);
                    } 
                    else
                    {
                        wake_.wait(lock, stop, has_work);
                    }

                    if (stop.stop_requested()) break;
                    if(!pending_) continue;

                    work.emplace(std::move(*pending_));
                    pending_.reset();
                    set_phase(WorkerPhase::running);
                }
                execute(*state, *work);
            }
            finish(WorkerPhase::stopped);
        }
        catch(const TaskCancelled&)
        {
            finish(WorkerPhase::stopped);
        }
        catch(...)
        {
            finish(WorkerPhase::failed, std::current_exception());
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::condition_variable result_ready_;
    WorkerPhase phase_ = WorkerPhase::initializing;
    std::string progress_;
    std::exception_ptr error_;
    std::optional<Result> result_;
    std::optional<Work> pending_;
    std::stop_source operation_stop_{std::nostopstate};
    std::jthread worker_;
};
} // namespace renderer::task_execution
