#!/bin/bash

set -u


apt-get update
apt-get upgrade
apt-get -y install emacs ubuntu-dev-tools
