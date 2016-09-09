#!/bin/bash

set -u

apt-get update
apt-get upgrade -y

apt-get -y install emacs ubuntu-dev-tools mpich libmpich-dev libcurl4-openssl-dev autoconf libtool libmxml-dev libtool cmake 

cp /vagrant/vagrant-scripts/install_hdf5tune*.sh /home/vagrant/Desktop

cd /home/vagrant/Desktop

/home/vagrant/Desktop/install_hdf5tune_deps.sh
/home/vagrant/Desktop/install_hdftune.sh

git clone http://github.com/Unidata/netcdf-c /home/vagrant/Desktop/netcdf-c
cd /home/vagrant/Desktop/netcdf-c
git checkout h5tuner

chown -R vagrant:vagrant /home/vagrant
