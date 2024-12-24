#!/bin/bash

if [ -z "$1" ]; then
  echo "Usage: $0 <shard_count>"
  exit 1
fi

./app/build-release/tcp_server --smp $1 --cpuset=0-$1 &

wait
