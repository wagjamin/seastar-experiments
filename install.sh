#!bin/bash

sudo apt install -y build-essential && \
sudo ./install-dependencies.sh
git submodule update --init --recursive && \
cd dpdk && \
meson setup build && \
cd build && \
ninja -j64 && \
cd ../../ && \
./configure.py --mode=release --compile-commands-json --enable-dpdk && \
ninja -C build/release -j64 && \
sudo ninja -C build/release install && \
sudo sh -c "echo 1024 > /proc/sys/vm/nr_hugepages" 


# optional: mount huge pages for `--hugepages /mnt/huge --memory 1G` parameters
sudo sh -c "echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages" && \
sudo mkdir /mnt/huge && \
sudo mount -t hugetlbfs nodev /mnt/huge
