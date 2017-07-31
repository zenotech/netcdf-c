Parallel IO Automatic Optimization {#pario_auto_optimization}
======================================

[TOC]

In cooperation with Engility and the HDF group, you are now able to optimize parallel I/O parameters for a given dataset, using a library and python script provided by the HDF group.  The tool which provides this functionality has two components; the injection library `libautotuner.so` and the python script which tunes the parameters, `h5evolve`.

## H5Tuner

H5Tuner may be downloaded and installed [from the H5Tuner project page on BitBucket](https://bitbucket.hdfgroup.org/projects/ENGILITY/repos/h5tuner/browse).  From the project,

> The goal of the H5Tuner component is to develop an autonomous parallel I/O parameter injector for scientific applications with minimal user involvement, allowing parameters to be altered without requiring source code modifications and a recompilation of the application.

### Requirements

H5Tuner requires the following packages:

* zlib
* hdf5
* MiniXML
* Pyevolve

Instructions for downloading and installing these software packages [may be found here](https://bitbucket.hdfgroup.org/plugins/servlet/readmeparser/display/ENGILITY/h5tuner/atRef/refs/heads/master/renderFile/doc/Requirements.md).
