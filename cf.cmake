#!/bin/sh

# These are installation dependent
PREFIX=t:/cygwin/usr/local
CPPATH="${PREFIX};t:/cygwin/usr/local/cmake/HDF5"
#NONC4="-DENABLE_NETCDF_4=OFF"

FLAGS="-DCMAKE_PREFIX_PATH=$CPPATH"
FLAGS="$FLAGS -DCMAKE_INSTALL_PREFIX=${PREFIX}"
FLAGS="$FLAGS -DENABLE_DAP_REMOTE_TESTS=true"
FLAGS="$FLAGS -DENABLE_DAP_AUTH_TESTS=true"

rm -fr build
mkdir build
cd build

cmake $FLAGS ..
#cmake --build .
#cmake --build . --target test
#cmake --build . --target install 
