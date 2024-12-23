#!bin/bash


# startup
ip_if0=$(cat ip_if0) && \
ip_if1=$(cat ip_if1) && \
ether_if1=$(cat ether_if1) && \
cd h-shuffle/benchmark/micro/userspace && \
mkdir -p app/build-release && \
cd app/build-release && \
cmake -DCMAKE_BUILD_TYPE=Release -GNinja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. && \
ninja -j 64




# run examples
cd /home/ubuntu/h-shuffle/benchmark/micro/userspace/app/build-release


# io_uring based, works without dpdk
./server --smp 1

./client --server_ip 172.31.32.20 --smp 1


# works with igb_uio and vfio-pci
sudo ./server --network-stack native --dpdk-pmd --dhcp 0 --host-ipv4-addr $ip_if1 --netmask-ipv4-addr 255.255.240.0 --smp 1

sudo ./client --server_ip 172.31.32.120 --network-stack native --dpdk-pmd --dhcp 0 --host-ipv4-addr $ip_if1 --netmask-ipv4-addr 255.255.240.0 --smp 1


# only works with vfio-pci, showed bad performance on c7gn.16xlarge
sudo ./server --network-stack native --dpdk-pmd --dhcp 0 --host-ipv4-addr $ip_if1 --netmask-ipv4-addr 255.255.240.0 --hugepages /mnt/huge --memory 1G --smp 1

sudo ./client --server_ip 172.31.32.120 --network-stack native --dpdk-pmd --dhcp 0 --host-ipv4-addr $ip_if1 --netmask-ipv4-addr 255.255.240.0 --hugepages /mnt/huge --memory 1G --smp 1
