#include <renderer/task_execution/session_worker.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
using namespace renderer::task_execution;
void require(bool value, const char* message)
{ if (!value) throw std::runtime_error(message); }
template<class Predicate> void until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Worker test timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct Counts
{
    std::atomic_int made{0}, destroyed{0}, commands{0}, maintenance{0};
    std::thread::id creator, destroyer;
};
struct State
{
    std::shared_ptr<Counts> counts;
    int value = 0;
    explicit State(std::shared_ptr<Counts> c) : counts(std::move(c))
    { counts->creator = std::this_thread::get_id(); ++counts->made; }
    ~State() { counts->destroyer = std::this_thread::get_id(); ++counts->destroyed; }
};
using Worker = SessionWorker<State, int>;
}
int main()
{
    try
    {
        auto counts = std::make_shared<Counts>();
        std::optional<int> retained;
        {
            Worker worker([counts](const TaskContext& task)
            { task.report("Initialize once"); return std::make_unique<State>(counts); },
                [](State& state) { return ++state.counts->maintenance < 4; });
            until([&] { return worker.status().accepting(); });
            until([&] { return counts->maintenance >= 4; });
            for (int i = 1; i <= 10; ++i)
            {
                require(worker.try_submit([](State& state, const TaskContext&)
                    { ++state.counts->commands; return ++state.value; }), "ready worker rejected request");
                until([&] { return worker.status().phase == WorkerPhase::completed; });
                require(!worker.status().accepting(), "Unconsumed result was marked replaceable");
                require(!worker.try_submit([](State&, const TaskContext&) { return 0; }),
                    "New command overwrote an unread result");
                retained = worker.try_take_result();
                require(retained && *retained == i, "State was not persistent");
                require(!worker.try_take_result(), "Result transferred twice");
            }
            require(counts->made == 1, "State factory was rerun");
            std::atomic_bool entered = false;
            require(worker.try_submit([&](State&, const TaskContext& task)
            {
                entered = true;
                while (!task.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                task.cancellation_point();
                return 0;
            }), "cancel task rejected");
            until([&] { return entered.load(); });
            require(!worker.try_submit([](State&, const TaskContext&) { return 0; }), "busy request was queued");
            worker.request_stop();
            until([&] { return worker.status().phase == WorkerPhase::cancelled; });
            bool cancelled = false;
            try { static_cast<void>(worker.wait_take_result()); }
            catch (const TaskCancelled&) { cancelled = true; }
            require(cancelled, "Blocking take did not surface cancellation");
            require(worker.try_submit([](State& state, const TaskContext&) { return ++state.value; }),
                "cancelled worker could not be reused");
            require(worker.wait_take_result() == 11, "State was lost after cancellation");
        }
        require(counts->destroyed == 1 && counts->creator == counts->destroyer,
            "State teardown did not run on its creating worker");
        require(counts->creator != std::this_thread::get_id(), "State created on caller");
        require(retained && *retained == 10, "Result did not survive executor teardown");
        {
            Worker worker([](const TaskContext&) -> std::unique_ptr<State>
            { throw std::runtime_error("expected init error"); });
            until([&] { return worker.status().phase == WorkerPhase::failed; });
            require(worker.status().error != nullptr, "Initialization error lost");
            require(!worker.try_submit([](State&, const TaskContext&) { return 0; }), "Failed factory accepted task");
        }
        {
            Worker worker([](const TaskContext&) { return std::make_unique<State>(std::make_shared<Counts>()); });
            until([&] { return worker.status().accepting(); });
            require(worker.try_submit([](State&, const TaskContext&) -> int
                { throw std::runtime_error("expected operation error"); }), "exception task rejected");
            until([&] { return worker.status().phase == WorkerPhase::failed; });
            require(worker.status().error != nullptr, "Operation error lost");
        }
        {
            std::atomic_bool maintenance_entered = false, release = false;
            Worker worker([](const TaskContext&) { return std::make_unique<State>(std::make_shared<Counts>()); },
                [&](State&) -> bool
                {
                    maintenance_entered = true;
                    while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    throw std::runtime_error("expected retirement failure");
                });
            until([&] { return maintenance_entered.load(); });
            const bool accepted = worker.try_submit([](State&, const TaskContext&) { return 1; });
            release = true;
            require(accepted, "request concurrent with maintenance was not accepted");
            until([&] { return worker.status().phase == WorkerPhase::failed; });
            require(worker.status().error != nullptr, "Maintenance failure was overwritten by queued state");
        }
        {
            std::atomic_bool started = false;
            auto worker = std::make_unique<Worker>([&](const TaskContext& task)
            {
                started = true;
                while (!task.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                task.cancellation_point();
                return std::make_unique<State>(std::make_shared<Counts>());
            });
            until([&] { return started.load(); });
            worker.reset(); // requests cooperative cancellation during initialization
        }
        // Move-only output; a single accepted command must finish without
        // a caller polling initialization, submitting again, or ticking a GUI.
        {
            using MoveWorker = SessionWorker<State, std::unique_ptr<int>>;
            std::atomic_bool factory_entered = false, release = false;
            MoveWorker worker([&](const TaskContext& task) {
                factory_entered = true;
                until([&] { return release.load() || task.stop_requested(); });
                task.cancellation_point();
                return std::make_unique<State>(std::make_shared<Counts>());
            }, {}, [](State&, const TaskContext&) { return std::make_unique<int>(27); });
            until([&] { return factory_entered.load(); });
            const bool still_initializing = worker.status().phase == WorkerPhase::initializing;
            const bool accepted = worker.try_submit([](State&, const TaskContext&) { return std::make_unique<int>(0); });
            release = true;
            require(still_initializing && !accepted, "Initial operation left an acceptance window");
            auto only_owner = worker.wait_take_result();
            require(only_owner && *only_owner == 27 && !worker.try_take_result(),
                "Initial move-only result not transferred exactly once");
        }
        // Failure of maintenance before the handoff takes precedence over a
        // successful but still unread operation. Use a gate, not timing.
        {
            std::atomic_bool completed = false, release = false;
            Worker worker([](const TaskContext&) { return std::make_unique<State>(std::make_shared<Counts>()); },
                [&](State&) {
                    if (completed.load()) {
                        until([&] { return release.load(); });
                        throw std::runtime_error("retire after result");
                    }
                    return false;
                }, [&](State&, const TaskContext&) { completed = true; return 17; });
            until([&] { return worker.status().phase == WorkerPhase::completed; });
            release = true;
            until([&] { return worker.status().phase == WorkerPhase::failed; });
            bool caught = false;
            try { static_cast<void>(worker.wait_take_result()); }
            catch (const std::runtime_error& error) { caught = std::string(error.what()) == "retire after result"; }
            require(caught, "Unread result masked maintenance failure");
        }
        std::cout << "Session worker: PASS (persistent state, reuse, backpressure, cancellation, failure, shutdown)\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
