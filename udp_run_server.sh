#!/bin/bash

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo "Usage: $0 <shard_count> <connections> [<other_ip>]"
  exit 1
fi
other_ip=${3:-"127.0.0.1"}  

./app/build-release/udp_duplex --smp $1 --cpuset=0-$(($1 - 1)) --other_ip $other_ip --connections $2 &

wait
