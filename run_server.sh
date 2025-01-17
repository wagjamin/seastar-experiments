#!/bin/bash

if [ -z "$1" ]; then
  echo "Usage: $0 <server_shards>"
  exit 1
fi
server_shards=$1


./app/build-release/tcp_server \
 --smp $server_shards \
  --cpuset=0-$(($server_shards - 1)) \
 # --network-stack native --dpdk-pmd --dhcp 0 --host-ipv4-addr 172.31.32.120 --netmask-ipv4-addr 255.255.240.0 \
