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

2. shared libraries or DLLs
 setenv HDF5_PLUGIN_PATH <plugin-dir>/plugins 
/usr/local/hdf5/lib/plugin”
on Unix and
“%ALLUSERSPROFILE%\hdf5\lib\plugin”


Compression Filter Parameters
=============================

In order to compress a variable, it must be
annotated with sufficient information to locate
the compression filter code (as a shared
library) and with a vector of parameters for
controlling the action of the compression filter.
This information is provided by defining two special
attributes for any variable for which some form of
compression filter is to be applied. Since these
attributes are special, the wil normally be invisible
when using __ncdump__ unless the -s flag is specified.

The two reserved attributes are defined as follows.

1. ''_Filter_ID'' -- a string specifying the filter to apply.
This string is either the numeric id (as a string) assigned by the HDF
group to the filter [3] or the name associated with that filter.
See the appendix for a currently defined list of names and associated
identifiers.  For example, the id string "bzip2" (case insensitive)
is the same as "307".

2. ''_Filter_Parameters'' -- a vector of unsigned integers representing the
parameters for controlling the operation of the specified filter.

The meaning of the parameters is, of course,
completely filter dependent and the filter
description [3] needs to be consulted. For
bzip2, for example, a single parameter is provided
representing the compression level.
It is legal to provide a zero-length set of parameters or,
equivalently, provide no ''_Filter_Parameters'' attribute
at all. Defaults are not provided, so this assumes that
the filter can operate with zero parameters.

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
<tr><td>N.A.<td>SZIP
<tr><td>307<td>BZIP2
<tr><td>32013<td>ZFP
<tr><td>32014<td>FPZIP
</table>
Note that ZIP and SZIP have no assigned numbers. For ZIP, this can
also be specified using the "_DeflateLevel" attribute.

If you would like to add to this list, send a message to
support-netcdf@unidata.ucar.edu.
