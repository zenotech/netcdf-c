#!/bin/bash

set -u

apt-get update
apt-get upgrade -y

apt-get -y install emacs ubuntu-dev-tools mpich libmpich-dev libcurl4-openssl-dev autoconf libtool libmxml-dev libtool
cp /vagrant/vagrant-scripts/install_hdf5tune*.sh /home/vagrant/Desktop

sudo -u vagrant /home/vagrant/Desktop/install_hdf5tune_deps.sh
sudo -u vagrant /home/vagrant/Desktop/install_hdf5tune.sh

git clone http://github.com/Unidata/netcdf-c /home/vagrant/Desktop/netcdf-c

chown -R vagrant:vagrant /home/vagrant
