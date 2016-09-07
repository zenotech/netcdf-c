#!/bin/bash

set -u


apt-get update
apt-get upgrade
apt-get -y install emacs ubuntu-dev-tools mpich libmpich-dev libcurl4-openssl-dev  
autoconf libtool libmxml-dev libtool
cp /vagrant/vagrant-scripts/*.sh /home/vagrant/Desktop
chown -R vagrant:vagrant /home/vagrant
