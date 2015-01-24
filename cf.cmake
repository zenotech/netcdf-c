rm -fr build
mkdir build
cd build
PREFIX=/usr/local
CPPATH="/machine/dmh/libs;${CMAKE_PREFIX_PATH}"
#NONC4="-DENABLE_NETCDF_4=OFF"
#HDF5="-DHDF5_LIB=${PREFIX}/lib/libhdf5.so -DHDF5_HL_LIB=${PREFIX}/lib/libhdf5_hl.so -DHDF5_INCLUDE_DIR=${PREFIX}/include"

FLAGS="-DCMAKE_PREFIX_PATH=$CPPATH"
FLAGS="$FLAGS -DCMAKE_INSTALL_PREFIX=${PREFIX}"
FLAGS="$FLAGS -DENABLE_DAP_REMOTE_TESTS=true"
FLAGS="$FLAGS -DENABLE_DAP_AUTH_TESTS=true"
#FLAGS="$FLAGS $HDF5"
cmake $FLAGS ..
cmake --build .
#make test
