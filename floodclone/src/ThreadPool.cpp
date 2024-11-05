#include "ThreadPool.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>


ThreadPool::ThreadPool(size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i) {

        // creat each worker with the task of infinitly looking at the work queue
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;

                {
                    // sleep until work is avialable or toled to exit
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });

                    if (this->stop && this->tasks.empty())
                        return;
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                    active_tasks++;  // Increment total active task count
                }

                task();
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    active_tasks--;  // Decrement total active task count
                    if (tasks.empty() && active_tasks == 0) {
                        idle_condition.notify_all();  // Signal pool is idle
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    join();
   
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        tasks.emplace(std::move(task));
    }
    condition.notify_one();
}


void ThreadPool::join() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();

    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::wait() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    idle_condition.wait(lock, [this] { return tasks.empty() && active_tasks == 0; });
}
