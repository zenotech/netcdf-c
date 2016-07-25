#!/bin/sh

set -e

if test "x$srcdir" = x; then
    srcdir=`pwd`
fi

echo "Generating nc file."

../ncgen/ncgen -4 -o gh295.nc gh295.cdl
echo "Copying nc file."

./nccopy -4 gh295.nc gh2952.nc

echo "Finished"
