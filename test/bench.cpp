#include <iostream>
#include <string>
#include <set>

#include <asio.h>


class bench
{
public:
    bench(asio::io_context & io_context, const tcp::endpoint & endpoint, size_t client_num, size_t require_num, size_t body_size )
        : io_context_(io_context)
        , endpoint_(endpoint)
        , require_num_(require_num)
        , req_num_(0)
        , resp_num_(0)
        , failed_num_(0)
    {
        clients_.reserve(client_num);
        for(size_t i = 0; i < client_num; ++i)
        {
            clients_.push_back(ClientData{ tcp::socket{io_context}, 1, 0});
        }

        body_.insert(0, body_size, 'a');
    }

    void start()
    {
        for(size_t i = 0; i < clients_.size(); ++i)
        {
            do_connect(i);
        }
    }

    void do_connect(size_t i)
    {
        if(clients_[i].socket_.is_open())
            clients_[i].socket_.close();
        clients_[i].socket_.open(endpoint_.protocol());
        clients_[i].socket_.async_connect(endpoint_, [this, i](const error_code & ec)
        {
            on_connect(i, ec);
        });
    }

    void on_connect(size_t i, const error_code & ec)
    {
        if(ec)
        {
            //clients_[i].socket_.async_connect(endpoint_, [this, i](const error_code & ec)
            //{
            //    on_connect(i, ec);
            //});
            std::cout << "on connect: " << ec.message() << std::endl;
            return;
        }
        do_write(i);
        do_read(i);
    }

    void do_write(size_t i)
    {
        if(require_num_ == req_num_)
            return;

        clients_[i].request_ = http_request{};
        clients_[i].request_.method(http::verb::post);
        clients_[i].request_.target("/pipeline");
        clients_[i].request_.body() = body_;
        clients_[i].request_.set("seq", std::to_string(clients_[i].index_));
        clients_[i].request_.prepare_payload();

        //std::cout << clients_[i].request_ << std::endl;

        http::async_write(clients_[i].socket_, clients_[i].request_, [this, i](const error_code & ec, size_t )
        {
            if(ec)
            {
                std::cout << "on write: " << ec.message() << std::endl;
                //do_connect(i);
                return;
            }
            
            do_write(i);
        });

        ++ clients_[i].index_;
        ++ req_num_;
        // std::cout << "req num: " << req_num_ << std::endl;
    }

    void do_read(size_t i)
    {
        clients_[i].response_ = http_response{};
        http::async_read(clients_[i].socket_, clients_[i].buffer_, clients_[i].response_, [this, i](const error_code & ec, size_t )
        {
            if(resp_num_ == require_num_)
                return;
            
            if(++resp_num_ == require_num_)
            {
                std::cout << "finish" << std::endl;
                for(auto & i : clients_)
                {
                    error_code ec;
                    i.socket_.shutdown(tcp::socket::shutdown_both, ec);
                }
                return;
            }
            
            if(ec)
            {
                std::cout << "on read: " << ec.message() << std::endl;
                ++ failed_num_;
                return;
            }
            
            size_t idx = std::strtoull(clients_[i].response_["seq"].data(), nullptr, 10);
            //std::cout << idx << std::endl;
            if(idx != clients_[i].resp_index_+1)
            {
                std::cout << "index error, prev: " << clients_[i].resp_index_ << ", current: " << idx << std::endl;
            }
            clients_[i].resp_index_ = idx;
            do_read(i);
        });
    }

private:
    asio::io_context & io_context_;

    tcp::endpoint endpoint_;

    struct ClientData
    {
        tcp::socket socket_;
        size_t index_;
        size_t resp_index_;
        beast::flat_buffer buffer_;
        http_request request_;
        http_response response_;
    };

    std::vector<ClientData> clients_;

    std::string body_;

public:
    size_t require_num_;
    size_t req_num_;
    size_t resp_num_;
    size_t failed_num_;
};

int main(int argc, char* argv[])
{

    try
    {
        tcp::endpoint endpoint{asio::ip::address::from_string("127.0.0.1"), 12345};
        asio::io_context io_context{1};

        bench b{io_context, endpoint
            , std::strtoull(argv[1], nullptr, 10)
            , std::strtoull(argv[2], nullptr, 10)
            , std::strtoull(argv[3], nullptr, 10)
        };
        
        b.start();

        asio::steady_timer timer{io_context};
          
        std::function<void()> start_timer;
        start_timer = [&start_timer, &b, &timer]()
        {
            if(b.resp_num_ == b.require_num_)
                return;
            timer.expires_from_now(asio::chrono::seconds(1));
            timer.async_wait([&b, &start_timer](const error_code & ec)
            {
                if(ec)
                    return;
                std::cout << "require num: " << b.require_num_ << std::endl;
                std::cout << "req num: " << b.req_num_<< std::endl;
                std::cout << "resp num: " << b.resp_num_ << std::endl;
                std::cout << "failed num: " << b.failed_num_ << std::endl;
                start_timer();
            });
        };
        start_timer();
        
        //asio::signal_set sigs(io_context);
        //sigs.add(SIGINT);
        //sigs.add(SIGHUP);
        //sigs.add(SIGTERM);
        //sigs.add(SIGQUIT);
        //sigs.async_wait([&io_context](const error_code & ec, int sig)
        //{
        //    io_context.stop();
        //});

        // Run the server until stopped.
        io_context.run();

    }
    catch (std::exception& e)
    {
        std::cerr << "exception: " << e.what() << "\n";
    }

    return 0;
}
