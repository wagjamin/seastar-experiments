project(seastar_bench)
cmake_minimum_required (VERSION 3.13)

find_package (Seastar REQUIRED)

add_executable (server
  server.cpp)
target_link_libraries (server
  Seastar::seastar)

add_executable (tcp_server
  tcp_server.cpp)
target_link_libraries (tcp_server
  Seastar::seastar)

add_executable (client
  client.cpp)
target_link_libraries (client
  Seastar::seastar)
