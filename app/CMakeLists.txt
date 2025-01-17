project(seastar_bench)
cmake_minimum_required (VERSION 3.13)

set (CMAKE_CXX_STANDARD 23)

find_package (Seastar REQUIRED)

add_executable (tcp_server
  tcp_server.cpp)
target_link_libraries (tcp_server
  Seastar::seastar)

add_executable (tcp_client
  tcp_client.cpp)
target_link_libraries (tcp_client
  Seastar::seastar)
