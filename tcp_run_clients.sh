#!/bin/bash

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <num_clients> <server_shards> <connections_per_client>"
  exit 1
fi

num_clients=$1
server_shards=$2
connections_per_client=$3

for ((i=0; i<num_clients; i++)); do
  cpuset_start=$((i * server_shards))
  cpuset_end=$((cpuset_start + server_shards))
  echo "Starting client $i on CPUs ${cpuset_start}-${cpuset_end}"
  ./app/build-release/tcp_client \
    --smp $server_shards \
    --cpuset=${cpuset_start}-${cpuset_end} \
    --connections $connections_per_client &
done

wait 