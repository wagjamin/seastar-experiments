## Microbenchmarks for High-Performance Networking Libraries

This directory contains microbenchmarks for high-performance networking libraries.
The goal here is simple: measure how many 16-byte packets per second (PPS) the library can send between two nodes.

We want to run the microbenchmark on 200 Gbit/s Graviton instances.

## Seastar

[Seastar](https://seastar.io/) is ScyllaDB's high-performance networking framework.
Seastar has different backends, including a DPDK one.

Note that we point seastar to `871079a9ae` which is a recent commit off master as of October 2024.

To build the microbenchmark locally, run:
```sh
# Local seastar installation as a static library. Requires sudo priviliges for system installation.
./install_seastar.sh
# Go into custom microbenchmark
mkdir -p app/build-debug
cd app/build-debug
# And build the microbenchmark binaries. Note that compilation with clang doesn't work
cmake -DCMAKE_BUILD_TYPE=Debug -GNinja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_EXPORT_COMPILE_COMMANDS=1 ..
ninja
```

After that, you can easily run both the server and the client:
```sh
# Start the server, listens for UDP packets on ports [1200, ..., 1200 + <num_shards>[
./server [--smp <num_shards>]

# Start the client, sends UDP packets to ports [1200, ..., 1200 + <num_shards>[ on <target_ip>
# If no target IP is provided, simply sends to 127.0.0.1
./client [--smp <num_shards>] [--server_ip "<target ip>"]
```

Then run:
```sh
sudo sysctl -w kernel.perf_event_paranoid=1
```

Plot:
- Receive PPS given a certain number of CPU cores
- No acknowledging, server just sends packets and we see what arrives on the consumer side
