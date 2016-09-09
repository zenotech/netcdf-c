#!/bin/bash
#
# Utility script for downloading and installing h5tuner

set -e

echo "==================="
echo "Cloning from HDFGroup/H5Tuner"
echo "==================="

rm -rf H5Tuner

git clone git://github.com/HDFGroup/H5Tuner.git
cd H5Tuner

autoreconf -if

CC=`which mpicc` ./configure && make -j 4
cp src/libautotuner.so $HOME/Desktop
cp examples/config.xml $HOME/Desktop
