sudo /home/ubuntu/h-shuffle/submodules/dpdk/usertools/dpdk-devbind.py --status

# from igb_uio to vfio_pci
sudo rmmod igb_uio && \
sudo rmmod uio && \
sudo modprobe vfio enable_unsafe_noiommu_mode=1 && \
sudo modprobe vfio-pci && \
cd /home/ubuntu/h-shuffle/submodules && \
sudo dpdk/usertools/dpdk-devbind.py --bind=vfio-pci 00:06.0

# from vfio_pci to igb_uio
sudo modprobe -r vfio_pci && \
cd /home/ubuntu/h-shuffle/submodules/dpdk-kmods/linux/igb_uio && \
sudo modprobe uio && \
sudo insmod igb_uio.ko && \
cd ../../.. && \
sudo dpdk/usertools/dpdk-devbind.py --bind=igb_uio 00:06.0
