#!/bin/bash

if [ -z "$1" ]; then
  echo "Usage: $0 <number_of_servers>"
  exit 1
fi

num_servers=$1

for ((i=0; i<num_servers; i++)); do
  cpuset_start=$i
  cpuset_end=$((i+1))
  ./app/build-release/tcp_server --smp 1 --cpuset=${cpuset_start}-${cpuset_end} --server_offset $i &
done

wait
