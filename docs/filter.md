Filter Support in netCDF Enhanced {#compress}
=================================

[TOC]

Introduction {#compress_intro}
==================

The HDF5 library (1.8.11 and later) 
supports a general filter mechanism to apply various
kinds of filters to datasets before reading or writing. 
The netCDF enhanced (aka netCDF-4) library inherits this
capability since it depends on the HDF5 library.

Filters assume that a variable has chunking
defined and each chunk is filtered before
writing and "unfiltered" after reading and
before passing the data to the user.

The most common kind of filter is a compression-decompression
filter, and that is the focus of this document.

HDF5 supports dynamic loading of compression filters using the following
process for reading of compressed data.

1. Assume that we have a dataset with one or more variables that
were compressed using some algorithm. How the dataset was compressed
will be discussed subsequently.

2. Shared libraries or DLLs exist that implement the compress/decompress
algorithm. These libraries have a specific API so that the HDF5 library
can locate, load, and utilize the compressor/.

These libraries are expected to installed in a specific
directory. The default directory is
* "/usr/local/hdf5/lib/plugin” for Linux systems, or
* “%ALLUSERSPROFILE%\hdf5\lib\plugin” for Windows systems.

The default can be overridden by setting the environment
variable __HDF5_PLUGIN_PATH__.

Enablinmg A Compression Filter
=============================

In order to compress a variable, the netcdf-c library
must be given two pieces of information:
(1) some unique identifier for the filter to be uses
and (2) a vector of parameters for
controlling the action of the compression filter.

The meaning of the parameters is, of course,
completely filter dependent and the filter
description [3] needs to be consulted. For
bzip2, for example, a single parameter is provided
representing the compression level.
It is legal to provide a zero-length set of parameters or,
equivalently, provide no ''_Filter_Parameters'' attribute
at all. Defaults are not provided, so this assumes that
the filter can operate with zero parameters.

The two pieces of  information can be provided in either of two ways:
using __ncgen__ and via an API call.

Use __ncgen__
-------------

In a CDL file, the variable can be annotated with the
following two attributes.

1. ''_Filter_ID'' -- a string specifying the filter to apply.
This string is either the numeric id (as a string) assigned by the HDF
group to the filter [3] or the name associated with that filter.
See the appendix for a currently defined list of names and associated
identifiers.  For example, the id string "bzip2" (case insensitive)
is the same as "307".
2. ''_Filter_Parameters'' -- a vector of unsigned integers representing the
parameters for controlling the operation of the specified filter.

These are "special" attributes, which means that
they will normally be invisible
when using __ncdump__ unless the -s flag is specified.

Use The API
-------------
The include file, __netcdf_filter.h__ defines
an API method for setting the filter to be used
when writing a variable. The relevant signature is
as follows.
````
int nc_def_var_filter(int ncid, int varid, unsigned int id, size_t nparams, const unsigned int* parms);
````

References
==========

1. https://support.hdfgroup.org/HDF5/doc/Advanced/DynamicallyLoadedFilters/HDF5DynamicallyLoadedFilters.pdf
2. https://support.hdfgroup.org/HDF5/doc/TechNotes/TechNote-HDF5-CompressionTroubleshooting.pdf
3. https://support.hdfgroup.org/services/filters.html

Appendix A. Assigned Filter Names and Numbers
=============================================

This list is subject to change over time, although
backward copatibility is guaranteed.
<table>
<tr><th>Filter Number<th>Filter Name(s)
<tr><td>N.A.<td>ZIP
<tr><td>307<td>BZIP2
<tr><td>32013<td>ZFP
<tr><td>32014<td>FPZIP
</table>
Note that ZIP has no assigned number. For ZIP, this can
also be specified using the "_DeflateLevel" attribute.

If you would like to add to this list, send a message to
support-netcdf@unidata.ucar.edu.
