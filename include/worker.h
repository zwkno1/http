#pragma once

#include <list>

#include <asio.h>

template<typename WorkerFactory>
class worker_manager : private noncopyable
{
    typedef typename WorkerFactory::worker_ptr worker_ptr;
public:
    typedef shared_ptr<worker_manager<WorkerFactory> > ptr;

    worker_manager(WorkerFactory & factory)
        : io_context_()
        , work_guard_(asio::make_work_guard(io_context_))
    {
        worker_ = factory.create(io_context_);
    }

    ~worker_manager()
    {
        stop();
    }

    void handle_connection(asio::ip::tcp::socket && sock)
    {
        worker_->handle_connection(std::move(sock));
    }

    void run()
    {
        io_context_.run();
    }

    void stop()
    {
        io_context_.stop();
    }

    inline asio::io_context & context()
    {
        return io_context_;
    }

private:
    std::function<void(asio::ip::tcp::socket &&)> new_connection_callback_;

    asio::io_context io_context_;

    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;

    worker_ptr worker_;
};
