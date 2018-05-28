#pragma once

#include <deque>
#include <functional>

#include <functional>

#include "comm.h"
#include "http.h"

class http_client: public enable_shared_from_this<http_client>
{
private:
    enum SocketState
    {
        Connecting,
        Connected,
        Disconnected,
    };
public:
    typedef shared_ptr<http_client> ptr;

    explicit http_client(asio::io_context & context, const tcp::endpoint & endpoint, size_t pipeline_size)
        : strand_(context)
        , socket_(context)
        , socket_state_(Disconnected)
        , endpoint_(endpoint)
        , pipeline_size_(pipeline_size)
    {
    }

    ~http_client()
    {
        stop();
    }

    template<typename Request, typename Callback>
    bool request(Request && req, Callback && cb)
    {
        if(!is_connected())
            return false;

        if(callbacks_.size() >= pipeline_size_)
            return false;

        auto self = shared_from_this();
        auto request = std::make_shared<Request>(std::forward<Request>(req));
        http::async_write(socket_, *request,  asio::bind_executor(strand_, [self, request](const error_code & ec, std::size_t bytes)
        {
            self->on_write(ec, bytes);
        }));

        callbacks_.emplace_back(std::forward<Callback>(cb));

        return true;
    }

    void start()
    {
        do_connect();
    }

    void stop()
    {
        do_close(asio::error::operation_aborted);
    }

    inline bool is_connected()
    {
        return socket_state_ == Connected;
    }

    inline bool is_disconnected()
    {
        return socket_state_ == Disconnected;
    }

private:
    void do_connect()
    {
        if(!is_disconnected())
            return;

        if(!socket_.is_open())
        {
            error_code ec;
            socket_.open(endpoint_.protocol() ,ec);

            if(ec)
            {
                return;
            }
        }

        socket_state_ = Connecting;
        auto self = shared_from_this();
        socket_.async_connect(endpoint_, [self](const error_code & ec)
        {
            self->on_connect(ec);
        });
    }

    void on_connect(const error_code & ec)
    {
        if(ec)
        {
            return do_close(ec);
        }

        socket_state_ = Connected;
        do_read();
    }

    void do_read()
    {
        // Read a request
        response_ = http_response();
        auto self = shared_from_this();
        http::async_read(socket_, buffer_, response_, [self](const error_code & ec, bool close)
        {
            self->on_read(ec, close);
        });
    }

    void on_read(const error_code & ec, bool close)
    {
        // Happens when the timer closes the socket
        if(ec)
        {
            return do_close(ec);
        }

        if(!callbacks_.empty())
        {
            callbacks_.front()(error_code{}, response_);
            callbacks_.pop_front();
        }

        if(close)
        {
            return do_close(asio::error::connection_aborted);
        }

        do_read();
    }

    void on_write(const error_code & ec, std::size_t )
    {
        if(ec)
        {
            return do_close(ec);
        }
    }

    void do_close(const error_code & ec)
    {
        if(is_disconnected())
            return;
        // Send a TCP shutdown
        error_code ignore_ec;
        socket_.shutdown(tcp::socket::shutdown_send, ignore_ec);
        socket_.close(ignore_ec);
        socket_state_ = Disconnected;
        // At this point the connection is closed gracefully

        response_.clear();
        for(auto & i : callbacks_)
        {
            i(ec, response_);
        }
    }

    asio::io_context::strand strand_;

    tcp::socket socket_;

    SocketState socket_state_;

    beast::flat_buffer buffer_;

    http_response response_;

    std::deque<std::function<void(const error_code & ec, http_response &)> > callbacks_;

    tcp::endpoint endpoint_;

    size_t pipeline_size_;
};

typedef http_client::ptr http_client_ptr;
