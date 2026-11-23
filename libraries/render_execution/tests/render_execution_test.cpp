#include <renderer/render_execution/render_execution.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <utility>

namespace
{
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

// 出力スロットが一つのセッションを CPU 上で再現する．
// スレッド間で共有するのは，スロットの空き状態と最初に投入を断った通知だけ．
struct Gate
{
    std::atomic_bool available{false};
    std::binary_semaphore first_refusal{0};
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
        if (!gate_->available.load())
        {
            if (!notified_)
            {
                notified_ = true;
                gate_->first_refusal.release();
            }
            return false;
        }
        ++sequence_;
        return true;
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
    bool notified_ = false;
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
    require(gate->first_refusal.try_acquire_for(std::chrono::seconds(5)), "Initial batch never ran");
    require(!execution.try_take_result(), "Blocked request reported an image");
    require(!execution.try_request(), "A second request displaced a blocked request");
    gate->available = true; // 空きを通知しなくても，ワーカーが自分で投入を再試行する．
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
    require(gate->first_refusal.try_acquire_for(std::chrono::seconds(5)), "No backpressure");
    execution.request_stop();
    try { static_cast<void>(execution.wait_take_result()); }
    catch (const renderer::task_execution::TaskCancelled&) { return; }
    throw std::runtime_error("A cancelled request produced an image");
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
