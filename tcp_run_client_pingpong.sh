#!/bin/bash

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo "Usage: $0 <client_shards> <connections_per_client> [<server_ip>]"
  exit 1
fi

client_shards=$1
connections_per_client=$2
# Use third argument if provided, otherwise default to localhost
server_ip=${3:-"127.0.0.1"}  

# If we're connecting to a local server, pin the client to different cores from the
# server. 
# For this, check if there's a locally running tcp_server and get the --smp argument 
# from the running tcp_server process
server_smp_count=0

if [ -z "$3" ]; then
  server_smp_count=$(ps -C tcp_server_pingpong -o args= | sed -nE 's/.*--smp ([0-9]+).*/\1/p')

  if [ -z "$server_smp_count" ]; then
    echo "Error: No local tcp_server_pingpong found."
    exit 1
  fi  

  echo "Running tcp_server process found with --smp $server_smp_count"
else
  echo "Connecting to remote server at $server_ip"
fi

cpuset_start=$server_smp_count
cpuset_end=$((cpuset_start + client_shards - 1))

./app/build-release/tcp_client_pingpong \
  --smp $client_shards \
  --cpuset=${cpuset_start}-${cpuset_end} \
  --connections $connections_per_client \
  --server_ip "$server_ip" &

wait 