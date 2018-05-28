#pragma once

#include <asio.h>

class udp_socket : public enable_shared_from_this<udp_socket>
{
    struct operation
    {
        virtual void operator ()(shared_ptr<udp_socket> & sock) = 0;
    };

    template<typename T>
    struct send_op : operation
    {
        send_op(T && msg)
            : msg_(std::move(msg))
        {
        }

        void operator ()(shared_ptr<udp_socket> & sock)
        {
            sock->socket_.async_send(asio::buffer(msg_), [sock](const error_code & ec, std::size_t bytes)
            {
                sock->on_write(ec, bytes);
            });
        }

        T msg_;
    };

    template<typename T>
    struct send_to_op : operation
    {
        send_to_op(const udp::endpoint & endpoint, T && msg)
            : endpoint_(endpoint)
            , msg_(std::move(msg))
        {
        }

        void operator ()(shared_ptr<udp_socket> & sock)
        {
            sock->socket_.async_send_to(asio::buffer(msg_), endpoint_, [sock](const error_code & ec, std::size_t bytes)
            {
                sock->on_write(ec, bytes);
            });
        }

        std::string msg_;
        udp::endpoint endpoint_;
    };

public:
    typedef std::function<void(shared_ptr<udp_socket> , uint8_t * data, std::size_t size)> recive_callback;
    udp_socket(asio::io_context & context, recive_callback rc, std::size_t buffer_size, std::size_t max_queue_size)
        : socket_(context)
        , buffer_(buffer_size)
        , max_queue_size_(max_queue_size)
        , recive_callback_(rc)
    {
        open();
    }

    template<typename T>
    bool send(T && msg)
    {
        if(operations_.size() >= max_queue_size_)
            return false;
        operations_.push_back(std::unique_ptr<operation>(new send_op<T>(std::move(msg))));
        if(operations_.size() == 1)
            do_write();
        return true;
    }

    template<typename T>
    bool send_to(const udp::endpoint & endpoint, T && msg)
    {
        if(operations_.size() >= max_queue_size_)
            return false;
        operations_.push_back(std::unique_ptr<operation>(new send_to_op<T>(endpoint, std::move(msg))));
        if(operations_.size() == 1)
            do_write();
        return true;
    }

    void open()
    {
        socket_.open(udp::v4());
    }

    void open(error_code & ec)
    {
        socket_.open(udp::v4(), ec);
    }

    void bind(const udp::endpoint & endpoint)
    {
        socket_.bind(endpoint);
    }

    void bind(const udp::endpoint & endpoint, error_code & ec)
    {
        socket_.bind(endpoint, ec);
    }

    void connect(const udp::endpoint & endpoint)
    {
        socket_.connect(endpoint);
    }

    void connect(const udp::endpoint & endpoint, error_code & ec)
    {
        socket_.connect(endpoint, ec);
    }

    void start()
    {
        do_read();
    }

    void do_write()
    {
        auto self = shared_from_this();
        (*operations_.front())(self);
    }

    void on_write(const error_code & ec, std::size_t bytes)
    {
        (void)bytes;
        operations_.pop_front();
        if(!ec)
        {
            if(!operations_.empty())
                do_write();
        }
    }

    void do_read()
    {
        auto self = shared_from_this();
        socket_.async_receive_from(asio::buffer(buffer_), peer_, [self](const error_code & ec, std::size_t bytes)
        {
            self->on_read(ec, bytes);
        });
    }

    void on_read(const error_code & ec, std::size_t bytes)
    {
        if(!ec)
        {
            recive_callback_(shared_from_this(), buffer_.data(), bytes);
            do_read();
        }
    }

    udp::endpoint local_endpoint()
    {
        udp::endpoint ep;
        error_code ec;
        ep = socket_.local_endpoint(ec);
        return ep;
    }

private:
    udp::socket socket_;

    udp::endpoint peer_;

    std::vector<uint8_t> buffer_;

    std::size_t max_queue_size_;

    recive_callback recive_callback_;

    std::list<std::unique_ptr<operation> > operations_;
};
