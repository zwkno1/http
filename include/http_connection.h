#pragma once

#include <array>
#include <functional>

#include <asio.h>

//an HTTP server connection

template <typename ConnectionPtr>
class http_context
{
    const static std::size_t invalid_index = std::numeric_limits<std::size_t>::max();
public:
    // disable copy
    http_context(const http_context &) = delete;
    http_context & operator= (const http_context &) = delete;
    
    http_context(const ConnectionPtr & c, std::size_t index)
        : connection_(c)
        , index_(index)
    {
    }
    
    http_context(http_context && other)
        : connection_(other.connection_)
        , index_(other.index_)
    {
        other.connection_.reset();
        other.index_ = invalid_index;
    }
    
    http_context & operator=(const http_context && other)
    {
        connection_ = other.connection_;
        index_ = other.index_;
        other.connection_.reset();
        other.index_ = invalid_index;
    }
    
    http_response & response()
    {
        return connection_->response(index_);
    }
    
    const http_response & response() const
    {
        return connection_->response(index_);
    }
    
    ConnectionPtr & connection()
    {
        return connection_;
    }
    
    bool commit() 
    {
        // only call once
        if(index_ == invalid_index)
            return false;
        bool result = connection_->commit(index_);
        index_ = invalid_index;
        return result;
    }
    
private:
    ConnectionPtr connection_;
    
    std::size_t index_;
};

class http_connection : public enable_shared_from_this<http_connection>
{
    struct http_pipeline
    {
        http_pipeline(std::size_t size)
        : first_(0)
        , last_(0)
        , data_ (size)
        {
        }
        
        bool full()
        {
            return first_ == (last_+1)% data_.size();
        }
        
        bool ready()
        {
            return (first_ != last_) && data_[first_].ready_;
        }
        
        http_response & front()
        {
            assert(first_ != last_);
            return data_[first_].response_;
        }
        
        void pop()
        {
            assert(first_ != last_);
            data_[first_].reset();
            first_ = (first_+1)%data_.size();
        }
        
        void push()
        {
            assert(first_ != (last_+1)%data_.size());
            last_ = (last_+1)%data_.size();
        }
        
        std::size_t consume()
        {
            assert(first_ != (last_+1)%data_.size());
            return last_;
        }
        
        void commit(std::size_t index)
        {
            assert(data_[index].ready_ == false);
            data_[index].ready_ = true;
        }
        
        struct pipeline_data
        {
            pipeline_data()
                : ready_(false)
            {
            }
            
            void reset()
            {
                ready_ = false;
                response_ = http_response{};
            }
            
            bool ready_;
            http_response response_;
        };
        
        std::size_t first_;
        std::size_t last_;
        std::vector<pipeline_data> data_;
    };
    
public:
    using context = http_context<shared_ptr<http_connection> >;
    
    #ifdef HTTP_CONNECTION_TRACE
    static uint64_t connectionCount_;
    #endif
    
    typedef std::function<void(context && , http_request &)> request_callback;
    
    typedef std::function<void(shared_ptr<http_connection>)> close_callback;
    
    explicit http_connection(tcp::socket && sock, request_callback rc, close_callback cc, std::size_t pipeline_size, std::size_t limit = std::numeric_limits<std::size_t>::max())
        : socket_(std::move(sock))
        , stopped_(false)
        , buffer_(limit)
        , pipeline_(pipeline_size)
        , request_callback_(rc)
        , close_callback_(cc)
    {
        #ifdef HTTP_CONNECTION_TRACE
        ++ connectionCount_;
        #endif
    }
    
    ~http_connection()
    {
        #ifdef HTTP_CONNECTION_TRACE
        --connectionCount_;
        #endif
    }
    
    void start()
    {
        do_read();
    }
    
    void stop()
    {
        if(stopped_)
            return;
        stopped_ = true;
        
        // Send a TCP shutdown
        error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        
        //auto self = shared_from_this();
        //socket_.get_io_context().post([this, self]()
        //{
        //    close_callback_(self);
        //});
    }
    
    tcp::endpoint local_endpoint()
    {
        tcp::endpoint ep;
        error_code ec;
        ep = socket_.local_endpoint(ec);
        return ep;
    }
    
    tcp::endpoint remote_endpoint()
    {
        tcp::endpoint ep;
        error_code ec;
        ep = socket_.remote_endpoint(ec);
        return ep;
    }
    
private:
    friend class http_context<shared_ptr<http_connection> >;
    
    http_response & response(std::size_t index)
    {
        return pipeline_.data_[index].response_;
    }
    
    bool commit(std::size_t index)
    {
        if(stopped_)
            return false;
        
        pipeline_.commit(index);
        
        // start write if index is first
        if(index == pipeline_.first_)
            do_write();
        
        return true;
    }
    
    void do_write()
    {
        // first index not ready or empty
        if(!pipeline_.ready())
        {
            return;
        }
        
        auto self = shared_from_this();
        http::async_write(socket_, pipeline_.front(), [this, self](const error_code & ec, std::size_t bytes)
        {
            if(stopped_)
            {
                return;
            }
            
            if(ec || pipeline_.front().need_eof())
            {
                // This means we should close the connection, usually because
                // the response indicated the "Connection: close" semantic.
                return do_stop();
            }
            
            // if pipeline is full, start read operation
            if(pipeline_.full())
            {
                pipeline_.pop();
                do_read();
            }
            else
            {
                pipeline_.pop();
            }
            
            do_write();
        });
    }
    
    void do_read()
    {
        // Read a request
        if(pipeline_.full())
        {
            return;
        }
        
        std::size_t index = pipeline_.consume();
        request_ = http_request{};
        // read until pipeline is full
        auto self = shared_from_this();
        http::async_read(socket_, buffer_, request_, [this, self, index](const error_code & ec, std::size_t bytes)
        {
            if(stopped_)
            {
                return;
            }
            
            if(ec)
            {
                return do_stop();
            }
            
            pipeline_.push();
            request_callback_(context{self, index}, request_);
            do_read();
        });
    }
    
    void do_stop()
    {
        if(stopped_)
            return;
        stopped_ = true;
        
        // Send a TCP shutdown
        error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        
        close_callback_(shared_from_this());
        
        // At this point the connection is closed gracefully
    }
    
    tcp::socket socket_;
    
    bool stopped_;
    
    beast::flat_buffer buffer_;
    
    http_request request_;
    
    http_pipeline pipeline_;
    
    request_callback request_callback_;
    
    close_callback close_callback_;
};

typedef shared_ptr<http_connection> http_connection_ptr;

#ifdef HTTP_CONNECTION_TRACE
uint64_t http_connection::connectionCount_;
#endif

