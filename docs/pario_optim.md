Parallel IO Automatic Optimization {#pario_auto_optimization}
======================================

[TOC]

Overview {#h5tuner_overview}
=============

In cooperation with Engility<sup>1</sup> and the HDF group<sup>2</sup>, you are now able to optimize parallel I/O parameters for a given dataset using a library and python script provided by the HDF group. The tool which provides this functionality has two components; the injection library `libautotuner.so` and the python script which tunes the parameters, `h5evolve`.  `h5evolve` uses **Genetic Programming** to determine the appropriate parameters for file I/O in a parallel/HPC environment.

Genetic Programming {#h5tuner_genetic_programming}
-------------------

*Genetic Programming* (GP) describes a specific type of machine learning.  A GP system uses a *genetic algorithm* (GA) to "evolve" the parameters in a system from a set of base (initial) values towards a more optimal set of parameters.  A GA typically uses some combination of previous parameters (in addition to random 'mutations') at each step, looking for an optimal combination. This is analogous to the way genes behave during biologic reproduction, hence the moniker 'Genetic'.  

The `h5evolve` script uses `libautotuner` to inject new parameters into the `libhdf5` I/O routines, looking for a set of optimal parameters which increase the I/O efficiency and speed for a given dataset.  This can be beneficial when writing very large datasets which are bound by I/O.  

For more information on genetic programming/algorithms, see the following resources:

* [http://geneticprogramming.com/](http://geneticprogramming.com/)
* [https://en.wikipedia.org/wiki/Genetic_programming](https://en.wikipedia.org/wiki/Genetic_programming)
* [https://www.britannica.com/technology/genetic-algorithm](https://www.britannica.com/technology/genetic-algorithm)

Installation and Reference Documentation {#h5tuner_links}
---------------------------------------------

Full documentation [may be browsed here](https://bitbucket.hdfgroup.org/projects/ENGILITY/repos/h5tuner/browse/doc). Individual documentation may be viewed here:

* <A href="https://bitbucket.hdfgroup.org/projects/ENGILITY/repos/h5tuner/browse/doc/Home.md">Home</A>
* <A href="https://bitbucket.hdfgroup.org/projects/ENGILITY/repos/h5tuner/browse/doc/Requirements.md">Requirements</A>
* <A href="https://bitbucket.hdfgroup.org/projects/ENGILITY/repos/h5tuner/browse/doc/Installation.md">Installation</A>
* <A href="https://bitbucket.hdfgroup.org/projects/ENGILITY/repos/h5tuner/browse/doc/Reference.md">Usage Reference</A>
* <A href="https://bitbucket.hdfgroup.org/projects/ENGILITY/repos/h5tuner/browse/doc/Example.md">Examples</A>

## Installation

> The following is adapted from the <A href="https://bitbucket.hdfgroup.org/projects/ENGILITY/repos/h5tuner/browse/doc/Installation.md">official installation documentation</A>.

### Downloading H5Tuner
H5Tuner can be downloaded from the HDF bitbucket [pages](https://bitbucket.hdfgroup.org/projects/ENGILITY/repos/h5tuner/browse) from which it can be obtained as zipball or cloned.

```
$ git clone https://bitbucket.hdfgroup.org/scm/engility/h5tuner.git
```
### Building H5Tuner

change directory to h5tuner

```
$ export AT_DIR=/path/to/H5Tuner/directory
$ cd ${AT_DIR}
```
Specify that the C compiler should be mpicc.
```
$ export CC=mpicc
```
Or the path to the mpicc for the MPI installation you want to use can be specified, if `mpicc` does not point to the right one.
```
$ ./autogen.sh
$ ./configure --with-hdf5=/path/to/hdf5 --with-mxml=/path/to/mxml --prefix=/install/directory
$ make && make install
$ ls /install/directory/lib/
libautotuner.so  libautotuner_static.a

```

### Testing H5Tuner shared
Once H5Tuner libraries are built, it is possible to check the build by running the built-in self-tests.
```
$ export LD_PRELOAD=/path/to/libautotuner.so
$ make check
```

> Currently it is necessary for the H5Tuner libraries to be built using the same HDF5 version used by the application being tuned.
> The preferred method is to build H5Tuner as shared library. Similarly, the required libraries will need to be build as shared ones, in order to be loaded dynamically.

### h5evolve

h5evolve.py will be generated from h5evolve.py.in by configure. Currently, there is no automatic installation for h5evolve.py. h5evolve can be run by just executing:
```
$ python h5evolve.py [options]


References
-----------

1: [http://www.engilitycorp.com/](http://www.engilitycorp.com/)
2: [http://www.hdfgroup.org](http://www.hdfgroup.org)
