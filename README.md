## Microbenchmarks for High-Performance Networking Libraries

This directory contains microbenchmarks for high-performance networking libraries.
The goal here is simple: measure how many 16-byte packets per second (PPS) the library can send between two nodes.

We want to run the microbenchmark on 200 Gbit/s Graviton instances.

## Seastar

[Seastar](https://seastar.io/) is ScyllaDB's high-performance networking framework.
Seastar has different backends, including a DPDK one.

Note that we point seastar to `871079a9ae` which is a recent commit off master as of October 2024.

Clone the seastar repo including the submodule:
```sh
git clone --recurse-submodules https://github.com/wagjamin/seastar-experiments.git
```
Checkout this minimal-example branch:
```sh
cd seastar-experiments && \
git checkout minimal-example
```

# Install dpdk
```sh
sudo apt-get install -y build-essential linux-image-extra-$(uname -r) && \
cd seastar && \
cd dpdk && \
meson setup build && \
cd build && \
ninja && \
sudo meson install
```

To build the microbenchmark locally, run:
```sh
# Local seastar installation as a static library. Requires sudo priviliges for system installation.
./install_seastar.sh
# Go into custom microbenchmark
mkdir -p app/build-release && \
cd app/build-release
# And build the microbenchmark binaries. Note that compilation with clang doesn't work
cmake -DCMAKE_BUILD_TYPE=Release -GNinja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. && \
cd ../.. && \
ninja -C app/build-release -j $(nproc)
```

### Running TCP Benchmarks
After that, you can easily run both the server and the client:
```sh
# Start the server, listens for UDP packets on ports [1200, ..., 1200 + <num_shards>[
./app/build-release/tcp_server [--smp <num_shards>] [--cpuset=0-<num_shards-1>] [--data_size <receive/send buffer size>]

# Start the client, sends UDP packets to ports [1200, ..., 1200 + <num_shards>[ on <target_ip>
# If no target IP is provided, simply sends to 127.0.0.1
./app/build-release/tcp_client [--smp <num_shards>] [--cpuset=0-<num_shards-1>] [--connections <connections_per_shard>] [--server_ip "<target ip>"] [--data_size <receive/send buffer size>]
```

By default, seastar will use the io_uring backend.
To enable full user-space networking, add these command line arguments:
```
--network-stack native --dpdk-pmd --dhcp 0 --host-ipv4-addr <user_space_nic_ip> --netmask-ipv4-addr 255.255.240.0
```

Example execution:
Localhost:
```sh
# Server
./app/build-release/tcp_server \
 --smp 1 \
  --cpuset=0-0

# Client
connections_per_client=128
./app/build-release/tcp_client \
  --smp 1 \
  --cpuset=1-1 \
  --connections $connections_per_client \
  --server_ip "127.0.0.1"
```

Cloud DPDK backend:
Setup: user-space server nic ip: 172.31.32.120, user-space client nic ip: 172.31.32.121
```sh
# Server
server_shards=1
./app/build-release/tcp_server \
 --smp $server_shards \
  --cpuset=0-$(($server_shards - 1)) \
 --network-stack native --dpdk-pmd --dhcp 0 --host-ipv4-addr 172.31.32.120 --netmask-ipv4-addr 255.255.240.0

# Client
client_shards=1
server_ip="172.31.32.120"
connections_per_client=1
./app/build-release/tcp_client \
  --smp $client_shards \
  --cpuset=0-$(($client_shards - 1)) \
  --connections $connections_per_client \
  --server_ip "$server_ip" \
  --network-stack native --dpdk-pmd --dhcp 0 --host-ipv4-addr 172.31.32.121 --netmask-ipv4-addr 255.255.240.0
```
Cloud Uring backend:
```sh
# Server
server_shards=1
./app/build-release/tcp_server \
 --smp $server_shards \
  --cpuset=0-$(($server_shards - 1)) \

# Client
client_shards=1
server_ip="172.31.32.20"
connections_per_client=1
./app/build-release/tcp_client \
  --smp $client_shards \
  --cpuset=0-$(($client_shards - 1)) \
  --connections $connections_per_client \
  --server_ip "$server_ip"
```