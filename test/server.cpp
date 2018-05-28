#include <iostream>
#include <string>
#include <set>

#include <asio.h>
#include <tcp_server.h>
#include <http_connection.h>

class http_worker
{
public:
    http_worker(asio::io_context & context, std::size_t index)
        : context_(context)
        , timer_(context)
    {
        std::cout << "start worker: " << index << std::endl;
        start_timer();
    }

    void start_timer()
    {
        timer_.expires_from_now(asio::chrono::milliseconds(5000));
        timer_.async_wait([this](const error_code & ec)
        {
            handle_timeout(ec);
        });
    }

    void handle_connection(asio::ip::tcp::socket && sock)
    {
        //std::cout << "new connection" << std::endl;
        auto req_cb = [this](http_connection::context && ctx, http_request & request)
        {
            handle_request(std::move(ctx), request);
        };
        auto close_cb = [this](http_connection_ptr conn)
        {
            handle_close(conn);
        };

        auto s = make_shared<http_connection>(std::move(sock), req_cb, close_cb, 10);
        connections_[s] = chrono::steady_clock::now();
        s->start();
    }

    void handle_request(http_connection::context && ctx, http_request & request)
    {
        if(connections_.find(ctx.connection()) == connections_.end())
            return;
        
        //std::cout << "handle request" << std::endl;
        connections_[ctx.connection()] = chrono::steady_clock::now();
        //std::cout << "request " << ctx.index() << ", body: " << request.body() << std::endl;
        http_response & response = ctx.response();
        response.body() = std::move(request.body());
        response.prepare_payload();
        auto iter = request.find("seq");
        if(iter != request.end())
        {
            response.set("seq", iter->value());
        }
        ctx.commit();

        // use timer
        //int r = std::rand()%100;
        //std::cout << "response after " << r << "ms, index: " << ctx.index() << std::endl;
        //auto t = std::make_shared<asio::steady_timer>(context_);
        //t->expires_from_now(std::chrono::milliseconds(r));
        //t->async_wait([t, ctx](const error_code & ec)
        //{
        //    if(ec)
        //    {
        //        std::cout << ec.message() << std::endl;
        //        return;
        //    }
        //    std::cout << "response " << ctx.index() << ", body: " << ctx.response().body() << std::endl;
        //    ctx.commit();
        //});
    }

    void handle_timeout(const error_code & ec)
    {
        std::cout << "connection num: " << http_connection::connectionCount_ << std::endl;
        if(!ec)
        {
            auto expire = chrono::steady_clock::now() - chrono::seconds(30);
            for(auto iter = connections_.begin(); iter != connections_.end();)
            {
                if(iter->second < expire)
                {
                    std::cout << "stop" << std::endl;
                    iter->first->stop();
                    iter = connections_.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }
            start_timer();
        }
    }

    void handle_close(http_connection_ptr conn)
    {
        //std::cout << ">>> remaining: " << connections_.size() << std::endl;
        connections_.erase(conn);
        std::cout << "handle close, remaining: " << connections_.size() << std::endl;
    }

    std::map<shared_ptr<http_connection>, chrono::steady_clock::time_point> connections_;

    asio::io_context & context_;

    asio::steady_timer timer_;
};

class http_worker_factory
{
public:
    typedef shared_ptr<http_worker> worker_ptr;

    http_worker_factory()
        : index_(0)
    {
    }

    worker_ptr create(asio::io_context & context)
    {
        return make_shared<http_worker>(context, index_++);
    }

    size_t index_;
};

int main(int argc, char* argv[])
{
    std::srand(std::time(nullptr));

    try
    {
        tcp::endpoint endpoint{asio::ip::address::from_string("0.0.0.0"), 12345};
        http_worker_factory factory;
        tcp_server<http_worker_factory> server{endpoint, factory};

        asio::signal_set sigs(server.get_io_context());
        sigs.add(SIGINT);
        sigs.add(SIGHUP);
        sigs.add(SIGTERM);
        sigs.add(SIGQUIT);
        sigs.async_wait([&server](const error_code & ec, int sig)
        {
            server.stop();
        });

        // Run the server until stopped.
        server.start(true);

        server.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "exception: " << e.what() << "\n";
    }

    return 0;
}
