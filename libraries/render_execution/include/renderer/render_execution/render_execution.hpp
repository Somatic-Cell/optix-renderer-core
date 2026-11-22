#pragma once 

#include <renderer/render_execution/render_session.hpp>
#include <renderer/task_execution/session_worker.hpp>

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <utility>

namespace renderer::render_execution
{

template<RenderSession Session>
class RenderExecution final
{
    using OptionalImage = decltype(std::declval<Session&>().take_latest_image());
public:
    using Image = typename OptionalImage::value_type;
private:
    using Worker = task_execution::SessionWorker<Session, Image>;

public:
    using Factory = typename Worker::Factory;

    explicit RenderExecution(Factory factory, std::size_t initial_advances = 1)
    : worker_(std::move(factory), [](Session& session) {return session.collect(); }, render_request(initial_advances))
    {}

    [[nodiscard]] bool try_request(std::size_t advances = 1)
    {
        return worker_.try_submit(render_request(advances));
    }

    [[nodiscard]] task_execution::WorkerStatus status() const { return worker_.status(); }
    [[nodiscard]] OptionalImage try_take_result() { return worker_.try_take_result(); }
    
    [[nodiscard]] Image wait_take_result() { return worker_.wait_take_result(); }

    void request_stop() noexcept {worker_.request_stop(); }

private:
    static typename Worker::Operation render_request(std::size_t advances)
    {
        if(advances == 0)
        {
            throw std::invalid_argument("A render request must contain at least one advance");
        }

        return [advances](Session& session, const task_execution::TaskContext& task) -> Image
        {
            task.report("Submitting render work...");
            auto remaining = advances;
            while (remaining != 0)
            {
                task.cancellation_point();
                if(session.try_advance())
                {
                    --remaining;
                }
                else{
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
            auto image = session.take_latest_image();
            if(!image)
            {
                throw std::logic_error("Render request completed without an image");
            }
            return std::move(*image);
        };
    }

    Worker worker_;
};

} // namespace renderer::render_execution