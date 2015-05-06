#include "canvas/scheduling/scheduler.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>

namespace canvas
{
    class scheduler::task_queue
    {
    public:
        auto cancelled() const -> bool { return m_cancelled; }

        void cancel()
        {
            m_cancelled = true;
            m_available.notify_all();
        }

        void enqueue(task_type task)
        {
            {
                std::unique_lock<std::mutex> lock{m_mutex};
                m_tasks.emplace_back(task);
            }

            m_available.notify_one();
        }

        auto dequeue() -> task_type
        {
            std::unique_lock<std::mutex> lock{m_mutex};
            m_available.wait(lock, [&] { return !m_tasks.empty() || m_cancelled; });

            if (!m_cancelled) {
                auto task = std::move(m_tasks.front());
                m_tasks.pop_front();
                return task;
            }

            return task_type{};
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_available;

        std::atomic<bool> m_cancelled{false};

        std::deque<task_type> m_tasks;
    };

    scheduler::scheduler()
        : m_tasks{std::make_unique<task_queue>()}
    {
        create_workers(std::thread::hardware_concurrency());
    }

    scheduler::scheduler(std::size_t worker_count)
        : m_tasks{std::make_unique<task_queue>()}
    {
        create_workers(worker_count);
    }

    scheduler::~scheduler()
    {
        m_tasks->cancel();

        for (auto& worker : m_workers) {
            worker.join();
        }
    }

    auto scheduler::worker_count() const -> std::size_t
    {
        return m_workers.size();
    }

    void scheduler::schedule_task(task_type task)
    {
        m_tasks->enqueue(task);
    }

    void scheduler::create_workers(std::size_t worker_count)
    {
        m_workers.reserve(worker_count);

        for (std::size_t i = 0; i < worker_count; ++i) {
            m_workers.emplace_back(&scheduler::run_tasks, this);
        }
    }

    void scheduler::run_tasks()
    {
        while (!m_tasks->cancelled()) {
            if (auto task = m_tasks->dequeue()) {
                task();
            }
        }
    }
}
