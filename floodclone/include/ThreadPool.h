#ifndef TRHEADPOOL_H
#define TRHEADPOOL_H

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(size_t threads);
    ~ThreadPool();
    void enqueue(std::function<void()> task);
    void join();
    void wait(); // waits until task ueue is empty

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;

    std::condition_variable idle_condition;
    std::atomic<int> active_tasks{0};
};


#endif