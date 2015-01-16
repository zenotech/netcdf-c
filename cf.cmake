rm -fr build
mkdir build
cd build
UL=/usr/local
PPATH="$UL"
#NONC4="-DENABLE_NETCDF_4=OFF"
HDF5="-DHDF5_LIB=${UL}/lib/libhdf5.so -DHDF5_HL_LIB=${UL}/lib/libhdf5_hl.so -DHDF5_INCLUDE_DIR=${UL}/include"

BZ="-DBZIP2_LIBRARY=/machine/dmh/libs/lib/libbz2.so -DBZIP2_INCLUDE_DIR=/machine/dmh/libs/include"
FPZIP="-DFPZIP_LIBRARY=/machine/dmh/libs/lib/libfpzip.so -DFPZIP_INCLUDE_DIR=/machine/dmh/libs/include"
ZFP="-DZFP_LIBRARY=/machine/dmh/libs/lib/libzfp.so -DZFP_INCLUDE_DIR=/machine/dmh/libs/include"

FLAGS="-DCMAKE_PREFIX_PATH=$PPATH"
FLAGS="$FLAGS -DCMAKE_INSTALL_PREFIX=${UL}"
FLAGS="$FLAGS -DCMAKE_PREFIX_PATH="$PPATH"
FLAGS="$FLAGS -DENABLE_DAP_REMOTE_TESTS=true
FLAGS="$FLAGS -DENABLE_DAP_AUTH_TESTS=true"
FLAGS="$FLAGS $BZ"
FLAGS="$FLAGS $FPZIP"
FLAGS="$FLAGS $ZFP"
FLAGS="$FLAGS $HDF5"
cmake $FLAGS ..
#cmake --build .
#make test
