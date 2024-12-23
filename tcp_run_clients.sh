#!/bin/bash

if [ -z "$1" ]; then
  echo "Usage: $0 <number_of_clients>"
  exit 1
fi

num_clients=$1

for ((i=0; i<num_clients; i++)); do
  cpuset_start=$i
  cpuset_end=$((i+1))
  ./app/build-release/tcp_client --smp 1 --cpuset=${cpuset_start}-${cpuset_end} --client_offset $i &
done

wait 