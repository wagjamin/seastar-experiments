#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <client_shards> <connections_per_client>"
  exit 1
fi

client_shards=$1
connections_per_client=$2

# If there's a local server running, pin the client to different cores from the
# server. 
# For this, check if there's a locally running tcp_server and get the --smp argument 
# from the running tcp_server process
server_smp_count=$(ps -C tcp_server -o args= | sed -nE 's/.*--smp ([0-9]+).*/\1/p')

if [ -z "$server_smp_count" ]; then
  server_smp_count=1
  echo "No running tcp_server process found."
else
  echo "Running tcp_server process found with --smp $server_smp_count"
fi

cpuset_start=$server_smp_count
cpuset_end=$((cpuset_start + client_shards))

./app/build-release/tcp_client \
  --smp $client_shards\
  --cpuset=${cpuset_start}-${cpuset_end} \
  --connections $connections_per_client &

wait 