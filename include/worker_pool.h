#pragma once

#include <list>
#include <vector>

#include <asio.h>
#include <worker.h>

/// A pool of worker objects.
template<typename WorkerFactory>
class worker_pool
        : private noncopyable
{
public:
    worker_pool(WorkerFactory & factory, std::size_t pool_size)
        : next_worker_(0)
        , factory_(factory)
    {
        if (pool_size == 0)
            pool_size = std::thread::hardware_concurrency();

        for (std::size_t i = 0; i < pool_size; ++i)
        {
            workers_.push_back(make_shared<worker_manager<WorkerFactory> >(factory_));
        }
    }

    void run()
    {
        // Create a pool of threads to run all of the worker.
        std::vector<std::shared_ptr<std::thread> > threads;

        for (auto i : workers_)
        {
            threads.push_back(std::make_shared<std::thread>([i]()
            {
                i->run();
            }));
        }

        // Wait for all threads in the pool to exit.
        for (auto i : threads)
            i->join();
    }

    void stop()
    {
        for(auto i : workers_)
            i->stop();
    }

    worker_manager<WorkerFactory> & get_worker_manager()
    {
        // Use a round-robin scheme to choose the next io_context to use.
        ++next_worker_;
        next_worker_ %= workers_.size();
        return *workers_[next_worker_];
    }

private:
    typedef asio::executor_work_guard<asio::io_context::executor_type> io_context_work;
    typedef typename worker_manager<WorkerFactory>::ptr worker_ptr;

    /// The pool of io_contexts.
    std::vector<worker_ptr> workers_;

    /// The next io_context to use for a connection.
    std::size_t next_worker_;

    WorkerFactory & factory_;
};
