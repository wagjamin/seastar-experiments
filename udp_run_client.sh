#!/bin/bash

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo "Usage: $0 <shard_count> <connections> [<other_ip>]"
  exit 1
fi

other_ip=${3:-"127.0.0.1"}  

# If we're connecting to a local server, pin the client to different cores from the
# server. 
# For this, check if there's a locally running tcp_server and get the --smp argument 
# from the running tcp_server process
server_smp_count=0

if [ -z "$3" ]; then
  server_smp_count=$(ps -C udp_duplex -o args= | sed -nE 's/.*--smp ([0-9]+).*/\1/p')

  if [ -z "$server_smp_count" ]; then
    echo "Error: No local udp_duplex found."
    exit 1
  fi  

  echo "Running udp_duplex process found with --smp $server_smp_count"
else
  echo "Connecting to remote server at $other_ip"
fi

cpuset_start=$server_smp_count
cpuset_end=$((cpuset_start + $1 - 1))

./app/build-release/udp_duplex \
  --data_size 64000 \
  --smp $1 \
  --cpuset=${cpuset_start}-${cpuset_end} \
  --connections $2 \
  --other_ip "$other_ip" \
  --my_offset 2048 \
  --other_offset 1024 &

wait 