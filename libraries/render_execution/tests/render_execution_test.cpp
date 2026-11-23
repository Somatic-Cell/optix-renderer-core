#include <renderer/render_execution/render_execution.hpp>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <stop_token>
#include <utility>

namespace
{
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

// 出力スロットが一つのセッションを CPU 上で再現する．
struct Gate
{
    std::mutex mutex;
    std::condition_variable_any released;
    bool available = false;
    std::binary_semaphore waiting{0};

    void release_slot()
    {
        {
            std::lock_guard lock(mutex);
            available = true;
        }
        released.notify_one();
    }
};

struct Image
{
    std::unique_ptr<int> sequence; // ムーブ専用の結果を受け渡せるか確認する．
};

class Session
{
public:
    explicit Session(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}

    bool try_advance()
    {
        std::lock_guard lock(gate_->mutex);
        if (!gate_->available) return false;
        ++sequence_;
        return true;
    }

    void wait_for_frame_slot(std::stop_token stop)
    {
        std::unique_lock lock(gate_->mutex);
        gate_->waiting.release();
        gate_->released.wait(lock, stop, [this] { return gate_->available; });
    }

    std::optional<Image> take_latest_image()
    {
        ++publications_;
        // 4 回分と 2 回分の要求に対し，各要求の最後に 1 枚ずつ画像を渡す．
        if (!((sequence_ == 4 && publications_ == 1) ||
              (sequence_ == 6 && publications_ == 2)))
            throw std::runtime_error("Unexpected publication boundary");
        return Image{std::make_unique<int>(sequence_)};
    }

    renderer::task_execution::MaintenanceStatus collect()
    {
        return renderer::task_execution::MaintenanceStatus::idle;
    }

private:
    std::shared_ptr<Gate> gate_;
    int sequence_ = 0;
    int publications_ = 0;
};

static_assert(renderer::render_execution::RenderSessionFor<Session, Image>);
static_assert(!renderer::render_execution::RenderSessionFor<Session, int>);
static_assert(!renderer::render_execution::RenderSession<int>);

using Execution = renderer::render_execution::RenderExecution<Session>;
using Task = renderer::task_execution::TaskContext;

void check_batch()
{
    auto gate = std::make_shared<Gate>();
    Execution execution([gate](const Task&) { return std::make_unique<Session>(gate); }, 4);

    // UI 側から要求や状態確認を送る前に，最初の要求が実行される．
    require(gate->waiting.try_acquire_for(std::chrono::seconds(5)), "Initial batch never waited for a slot");
    require(!execution.try_take_result(), "Blocked request reported an image");
    require(!execution.try_request(), "A second request displaced a blocked request");
    gate->release_slot();
    auto first = execution.wait_take_result();
    require(*first.sequence == 4, "Lost progress across refusal");
    require(!execution.try_take_result(), "Consumed a result twice");

    require(execution.try_request(2), "Follow-up batch was rejected");
    auto second = execution.wait_take_result();
    require(*second.sequence == 6 && *first.sequence == 4, "Invalid image or session lifetime");

    bool rejected = false;
    try { static_cast<void>(execution.try_request(0)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && execution.status().accepting(), "Empty batch changed the session");
}

void check_stop()
{
    auto gate = std::make_shared<Gate>();
    Execution execution([gate](const Task&) { return std::make_unique<Session>(gate); });
    require(gate->waiting.try_acquire_for(std::chrono::seconds(5)), "No backpressure");
    execution.request_stop();
    try
    {
        static_cast<void>(execution.wait_take_result());
        throw std::runtime_error("A cancelled request produced an image");
    }
    catch (const renderer::task_execution::TaskCancelled&) {}

    gate->release_slot();
    require(execution.try_request(4), "Cancelled session rejected a new request");
    require(*execution.wait_take_result().sequence == 4, "Cancellation changed the next batch");
}
}

int main()
{
    try
    {
        check_batch();
        check_stop();
        std::cout << "Render execution: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
