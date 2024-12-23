#!/usr/bin/env bash
#
# This bash script installs seastar locally at /usr/local as outlined in the seastar
# README: https://github.com/scylladb/seastar
# After this you can just do find_package (Seastar REQUIRED) in your CMAKE and things
# will be okay.

# git submodule update --init --recursive
cd seastar
sudo ./install-dependencies.sh
./configure.py --mode=release --prefix=/usr/local
sudo ninja -C build/release install

