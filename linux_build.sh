#!/bin/bash

# run using something similar to this:
# bash linux_build.sh python3.5m
# puts result in linux/x64

# requires:
# sudo apt-get install python3-dev

# build QConsole
pushd external/qconsole3/src
qmake qconsoledll.pro
make
make install
popd
# build executable
mkdir -p linux/x64
export PYTHON_LIB=$1
qmake NionUILauncher.pro
make clean
make
# copy executable to target directory
mv NionUILauncher linux/x64