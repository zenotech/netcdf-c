Documentation for Enhanced Compression Support (#compression)
==================================================

\brief The enhanced compression layer provides a generic mechanism
for using and adding compress/decompress filters on netcdf-4 files.

\tableofcontents

# Introduction

The enhanced compression layer provides a generic mechanism
for adding compress/decompress filters on netcdf-4 files.
Since this builds on the HDF5 filter mechanism, compression
is only available with netcdf-4 files.

Compression modules can be plugged into the netCDF library
(at compile time) and will allow netCDF
programs to read and write data using a variety of compression schemes.
This should be of particular interest to anyone concerned
about the size of the files being produced.

This document describes both the compression API
as well as how to add a new compression algorithm.

# Supported Schemes

Currently, the following schemes are supported.
- "zip"
- "szip"
- "bzip2"
- "fpzip" (from LLNL)
- "zfp" (from LLNL)

The first two, zip and szip, are directly supported by libhdf5 if it 
is compiled with zlib and (optionally) szip support.
Support for zlib is on by default.

The last three require that the netcdf library be compiled
with the library and include directory specified
in the *LDFLAGS* and *CPPFLAGS* environment variables.

Some of these can present a problem because the library API
may not entirely standardized.

# Extensions to *netcdf.h*

In order to support multiple kinds of compressors, the netcdf
API has been extended by adding declarations and functions into netcdf.h
The changes have been defined so as to avoid the need for
compression specific arguments.

## Declarations

First, a number of constants are defined.
- NC_COMPRESSION_MAX_NAME -- define the maximum size of the canonical
  name of the compression method: "zip", "bzip2", etc.
- NC_COMPRESSION_MAX_PARAMS -- define the maximum number of
  parameters that can be provided to the compression method.
  This must be the max of the NC_NELEMS_XXX in nc4compress.h.
- NC_COMPRESSION_MAX_DIMS -- define the max number of dimensions
  that all allowed for any of the compression schemes. This
  is currently less than NC_MAX_VAR_DIMS for implementation
  specific reasons.

The compression scheme parameters are stored in an array of
unsigned ints (32 bits). For the current set of algorithms,
the array conforms to the following union
(see nc4compress.h for the definitive declaration).
It is subject to change as new
schemes are added.

~~~~~~~~~~~~~~~~~~~~~~~~~
typedef union {
    unsigned int params[NC_COMPRESSION_MAX_PARAMS];//arbitrary 32 bit values
    struct {
        unsigned int level;
    } zip;
    struct {
        unsigned int level;
    } bzip2;
    struct {
        unsigned int options_mask;
        unsigned int pixels_per_block;
    } szip;
    struct {
        int isdouble;
        int prec; // number of bits of precision (zero = full)
        int rank;
        size_t chunksizes[NC_COMPRESSION_MAX_DIMS];
    } fpzip;
    struct {
        int isdouble;
        int prec;
        double rate;
        double tolerance;
        int rank;
        size_t chunksizes[NC_COMPRESSION_MAX_DIMS];
    } zfp;
} nc_compression_t;
~~~~~~~~~~~~~~~~~~~~~~~~~

### Chunking
Every time a compressor (or decompressor) is called,
it is given a block of data comprising one chunk of the variable.
The chunks are defined using the *nc_def_var_chunking*
procedure in netcdf.h. So for our variable, v, when compressing,
the compressor will be called with a pointer to a block of data
of size S x c1 x c2 ... x cN where S is the size of a data element
(e.g. 4 for a float) and c1 thru cN are the chunk sizes defined
for the variable.

## API

### nc_def_var_compress

The *nc_def_var_compress* function is a generization of
*nc_def_var_deflate*.  It sets compression settings for a
variable for any supported compression scheme.  It must be
called after nc_def_var and before nc_enddef.  The form of
the parameters is, of course, algorithm dependent.

~~~~~~~~~~~~~~~~~~~~~~~~~
int nc_def_var_compress(int ncid,
                        int varid,
                        const char* algorithm,
                        int nparams,
                        unsigned int* params);
~~~~~~~~~~~~~~~~~~~~~~~~~

The parameter semantics are as follows.

1. *ncid* -- NetCDF or group ID, from a previous call to nc_open(),
nc_create(), nc_def_grp(), or associated inquiry functions such as 
nc_inq_ncid().

2. *varid* -- Variable ID

3. *algorithm* -- This specifies the name of the compression
algorithm to use (e.g. "zip", "bzip2", etc).

4. *nparams* -- This specifies the number of valid paramters
in the params vector.

5. *params* -- This specifies the parameters for the specified
compression algorithm.

The possible return codes are as follows.
- *NC_NOERR* No error.
- *NC_ENOTNC4* Not a netCDF-4 file. 
- *NC_EBADID* Bad ncid.
- *NC_ENOTVAR* Invalid variable ID.
- *NC_ECOMPRESS* Invalid compression parameters.

### nc_inq_var_compress

The nc_inq_var_compress can be used
to find out compression settings of a var.
If any parameter is null, then no value will be returned.

~~~~~~~~~~~~~~~~~~~~~~~~~
int nc_inq_var_compress(int ncid,
                        int varid,
                        char**algorithmp,
                        int* nparamsp,
                        unsigned int* paramsp);
~~~~~~~~~~~~~~~~~~~~~~~~~

The parameter semantics are as follows.

1. *ncid* -- NetCDF or group ID, from a previous call to nc_open(),
nc_create(), nc_def_grp(), or associated inquiry functions such as 
nc_inq_ncid().

2. *varid* -- Variable ID

3. *deflatep* -- If this pointer is non-NULL, the name of the compression
algorithm for this variable will be stored into this parameter.
This is a constant and the caller should not free this value.
\ref ignored_if_null.

4. *nparamsp* -- The number of valid parameters will be stored here.
\ref ignored_if_null.

5. *paramsp* -- The parameters for the specified
compression algorithm will be stored into this vector.

The possible return codes are as follows.
- *NC_NOERR* -- No error.
- *NC_ENOTNC4* -- Not a netCDF-4 file. 
- *NC_EBADID* -- Bad ncid.
- *NC_ENOTVAR* -- Invalid variable ID.
- *NC_ECOMPRESS* -- Invalid compression parameters.


### nc_def_var_shuffle

In order to simplify the compression related API,
the shuffle flag is now handled like e.g. fletcher32,
as a separate set of procedures.

~~~~~~~~~~~~~~~~~~~~~~~~~
int nc_def_var_shuffle(int ncid, int varid, int shuffle);
~~~~~~~~~~~~~~~~~~~~~~~~~

The parameter semantics are as follows.

1. *ncid* -- NetCDF or group ID, from a previous call to nc_open(),
nc_create(), nc_def_grp(), or associated inquiry functions such as 
nc_inq_ncid().

2. *varid* -- Variable ID

3. *shuffle* -- 1 indicates turn on shuffle, 0 indicates turn it off.

The possible return codes are as follows.
- *NC_NOERR* -- No error.
- *NC_ENOTNC4* -- Not a netCDF-4 file. 
- *NC_EBADID* -- Bad ncid.
- *NC_ENOTVAR* -- Invalid variable ID.
- *NC_EINVAL* -- Invalid argument

~~~~~~~~~~~~~~~~~~~~~~~~~
int nc_inq_var_shuffle(int ncid, int varid, int *shuffle);
~~~~~~~~~~~~~~~~~~~~~~~~~

The parameter semantics are as follows.

1. *ncid* -- NetCDF or group ID, from a previous call to nc_open(),
nc_create(), nc_def_grp(), or associated inquiry functions such as 
nc_inq_ncid().

2. *varid* -- Variable ID

3. *shufflep* -- the current shuffle value for this variable is
                 stored thru this pointer.

The possible return codes are as follows.
- *NC_NOERR* -- No error.
- *NC_ENOTNC4* -- Not a netCDF-4 file. 
- *NC_EBADID* -- Bad ncid.
- *NC_ENOTVAR* -- Invalid variable ID.
- *NC_EINVAL* -- Invalid argument

# Algorithm Specific Notes

Suppose we have this variable.
~~~~~~~~~~
dimensions:
  d1 = ...;
  d2 = ...;
  d3 = ...;
  ...
  dN = ...;
int v(d1,d2,d3,...dN)
~~~~~~~~~~

## *fpzip* and *zfp*

The fpzip (and zfp) have the problem that they are only defined
for variables with 1 to 3 dimensions. So, some way must be defined
to support variables with N dimensions, where N is less than 3.

The following schemes have been defined to handle this case.
1. Prefix (the default) -- The first two dimensions are kept as is, and the
   third dimension is increased to cover all of the dimensions
   after the first two. So the above is treated as if it was defined
   as follows.
~~~~~~~~~~
int v(d1,d2,dM)
~~~~~~~~~~
   where dM is defined to have the size d3 x d4 ...x dN.
   Since we are actually dealing with chunks, the actual last chunk
   is treated as if it was of size c3 x d4 ...x cN.

2. Choice (M out of N) -- The idea here is to define the chunks so that
   at most three have chunk sizes greater than one. This potentially
   has significant consequences for performance because it can increase
   the number of chunks dramatically, which can significantly increase
   compression time.

The choice of scheme is determined as follows.

1. If all but three of the chunksizes are set to 1 (one),
   then the choice scheme will be used.
2. Otherwise, prefix will be used.

<!--
# Adding a New Compression Algorithm

Suppose we would like to add a new compression algorithm called *xpress*.
This requires the following basic steps.

1. Define a string name for the new algorithm, presumably "xpress"
   in this case.

2. Define a struct to add to the *nc_compression_t* union.
   For Example, it might be like this.
~~~~~~~~~~
struct {
 T1 param1;
 T2 param2;
} xpress;
~~~~~~~~~~
   where T1 and T2 are some defined type (e.g. int or float).
-->

# Compression Testing

The program nc_test4/tst_compress is set up to do some
simple testing of compression algorithms.
Its usage is as follows.
~~~~~~~~~~
tst_compress <options>
where the options are:
    [-d<int>] -- specify number of dimensions to use
    [-p<int>] -- specify precision (0=>full precision) (fpzip,zfp only)
    [-r<double>] -- specify rate (zfp only)
    [-t<double>] -- specify tolerance (zfp only)
    [-l<int>] -- specify level (zip,bzip2 only)
    [-b<int>] -- specify pixels-per-block (szip only)
    [-h] -- print this message
    [nozip|zip|szip|bzip2|fpzip|zfp] -- specify the algorithms to test
                                        (may be repeated)
~~~~~~~~~~

# Configuring Compression

In order for a compression algorithm to be "activated",
two criteria must be met.

## Automake
1. The ./configure flag --with-compression=c1,c2,...cn
   must be used and the name of the desired compression algorithm
   (e.g. bzip2) must appear as one of the comma separated ci.
2. The library for the compression algorithm must be accessible,
   typically thru the *LDFLAGS* and *CPPFLAGS* environment variables.

If *--with-compression* is not specified, then it defaults to zip only.

## Cmake
1. The flag *-DCOMPRESSION=c1,c2,...cn*
   must be used and the name of the desired compression algorithm
   (e.g. bzip2) must appear as one of the comma separated ci.
2. The library for the compression algorithm must be accessible.
   Typically, the flag *-DCMAKE_PREFIX_PATH=...* is used
   to specify these libraries.

If *-DCOMPRESSION* is not specified, then it defaults to zip only.
