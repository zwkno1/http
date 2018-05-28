#pragma once

#define HTTP_DISABLE_THREADS
#define HTTP_CONNECTION_TRACE

#ifdef HTTP_DISABLE_THREADS
#define BOOST_ASIO_DISABLE_THREADS
#endif

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <functional>
#include <chrono>

using noncopyable = boost::noncopyable;
using error_code = boost::system::error_code;

namespace asio = boost::asio;
using tcp = boost::asio::ip::tcp;
using udp = boost::asio::ip::udp;

namespace beast = boost::beast;
namespace http = boost::beast::http;
using http_response = http::response<http::string_body>;
using http_request = http::request<http::string_body>;

namespace chrono = std::chrono;

#ifdef HTTP_DISABLE_THREADS

template<typename _Tp>
using shared_ptr = std::__shared_ptr<_Tp, std::_Lock_policy::_S_single>;

template<typename _Tp>
using enable_shared_from_this = std::__enable_shared_from_this<_Tp, std::_Lock_policy::_S_single>;

template<typename _Tp>
using weak_ptr = std::__weak_ptr<_Tp, std::_Lock_policy::_S_single>;

template<typename _Tp, typename... _Args>
inline shared_ptr<_Tp> make_shared(_Args&&... args)
{
    return std::__make_shared<_Tp, std::_Lock_policy::_S_single, _Args...>(std::forward<_Args>(args)...);
}

#else

using std::shared_ptr;
using std::enable_shared_from_this;
using std::weak_ptr;
using std::make_shared;

#endif

namespace boost
{
namespace asio
{
namespace external
{

typedef asio::detail::socket_option::boolean<BOOST_ASIO_OS_DEF(SOL_SOCKET), SO_REUSEPORT> reuse_port;

}
}
}
