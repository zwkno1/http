#pragma once

#include <string>
#include <vector>

#include <asio.h>
#include <http_connection.h>
#include <worker_pool.h>

#ifdef HTTP_DISABLE_THREADS

template<typename WorkerFactory>
class tcp_server : private noncopyable
{
    typedef typename WorkerFactory::worker_ptr worker_ptr;

public:
    explicit tcp_server(const tcp::endpoint & endpoint, WorkerFactory & factory)
        : io_context_(1)
	    , endpoint_(endpoint)
        , acceptor_(io_context_)
        , worker_(factory.create(io_context_))
        , sock_(io_context_)
    {
    }

    void start(bool reuse_port = false)
    {
        // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
        acceptor_.open(endpoint_.protocol());
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        acceptor_.set_option(asio::external::reuse_port(reuse_port));
        acceptor_.bind(endpoint_);
        acceptor_.listen();
        start_accept();
    }

    /// Run the server's io_context loop.
    void run()
    {
        io_context_.run();
    }

    void stop()
    {
        io_context_.stop();
    }

    asio::io_context & get_io_context()
    {
        return io_context_;
    }

private:
    void start_accept()
    {
        acceptor_.async_accept(sock_, [this] (const error_code & err)
        {
            handle_accept(err);
        });
    }

    void handle_accept(const error_code & err)
    {
        if(!err)
        {
            worker_->handle_connection(std::move(sock_));
            start_accept();
        }
    }

    asio::io_context io_context_;

    tcp::endpoint endpoint_;

    /// Acceptor used to listen for incoming connections.
    asio::ip::tcp::acceptor acceptor_;

    worker_ptr worker_;

    tcp::socket sock_;
};

#else

template<typename WorkerFactory>
class tcp_server : private noncopyable
{
public:
    typedef worker_manager<WorkerFactory> worker;

    explicit tcp_server(const tcp::endpoint & endpoint, WorkerFactory & factory, std::size_t pool_size = 0)
        : worker_pool_(factory, pool_size)
        , io_context_(1)
	    , endpoint_(endpoint)
        , acceptor_(io_context_)
    {
    }

    void start(bool reuse_port = false)
	{
		acceptor_.open(endpoint_.protocol());
		acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_port(reuse_port));
        acceptor_.bind(endpoint_);
		acceptor_.listen();

		start_accept();
	}

    /// Run the server's io_context loop.
    void run()
    {
        std::thread accept_thread([this]()
        {
            io_context_.run();
        });
        worker_pool_.run();
        accept_thread.join();
    }

    void stop()
    {
        worker_pool_.stop();
        acceptor_.close();
    }

	asio::io_context & get_io_context()
	{
		return io_context_;
	}

private:
    void start_accept()
    {
        worker & w = worker_pool_.get_worker_manager();
        sock_ = make_shared<asio::ip::tcp::socket>(w.context());
        acceptor_.async_accept(*sock_, [this, &w] (const error_code & err)
        {
            handle_accept(w, err);
        });
    }

    void handle_accept(worker & w, const error_code & err)
    {
        if(!err)
        {
            auto sock = sock_;
            sock_.reset();
            w.context().post([&w, sock]()
            {
                w.handle_connection(std::move(*sock));
            });
            start_accept();
        }
    }

    worker_pool<WorkerFactory> worker_pool_;

	asio::io_context io_context_;

	tcp::endpoint endpoint_;

    /// Acceptor used to listen for incoming connections.
    asio::ip::tcp::acceptor acceptor_;

    shared_ptr<asio::ip::tcp::socket> sock_;
};

#endif
