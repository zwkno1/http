cmake_minimum_required(VERSION 2.8)

project(http_test)

set (CMAKE_CXX_STANDARD 11)

set(Boost_USE_STATIC_LIBS   ON)
find_package(Boost 1.66 COMPONENTS system REQUIRED )
include_directories(${Boost_INCLUDE_DIR})

find_package(Threads)

include_directories(../include ../asio_async_resolver/include)

file(GLOB_RECURSE HEADER_FILES "../include/*.h")
add_custom_target(headers SOURCES ${HEADER_FILES})

add_executable(http_server server.cpp)
target_link_libraries(http_server ${Boost_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        -lcares
	)

add_executable(http_bench bench.cpp)
target_link_libraries(http_bench ${Boost_LIBRARIES}
	    ${CMAKE_THREAD_LIBS_INIT}
		-lcares
	)
