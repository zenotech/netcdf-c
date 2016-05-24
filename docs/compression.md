Documentation for Enhanced Compression Support (#compression)
==================================================

\brief The enhanced compression layer provides a generic mechanism
for using and adding compress/decompress filters on netcdf-4 files.

\tableofcontents

# Introduction

The enhanced compression layer provides a generic mechanism for adding
new compress/decompress filters for netcdf-4 files. Since this builds on
the HDF5 filter mechanism, compression is only available with netcdf-4
files.

Compression modules currently must added at the time that the netcdf-c
library is built from source. Such modules allow netCDF programs to read
and write data using a variety of compression schemes.  This should be
of particular interest to anyone concerned about the size of the files
being produced.

This document primarily describes the compression API.  Some information
is also provided about how to add a new compression algorithm.

# Supported Schemes

Currently, the following schemes are supported.
- "zip"
- "szip"
- "bzip2"
- "fpzip" (from LLNL)
- "zfp" (from LLNL)

Zip (zlib) support is on by default because it is required already for
the HDF5 libraries.  Szip is also supported if szip support was compiled
into libhdf5 when it was built.

The last three require that the netcdf library be compiled with the
corresponding libraries and include files.  These are specified using
the *LDFLAGS* and *CPPFLAGS* environment variables -- for autoconf -- or
via arguments to cmake.

The last two (fpzip and zfp) are still under active development,
so there is some instability in their APIs.

# Extensions to *netcdf.h*

In order to support multiple kinds of compressors, the netcdf
API has been extended by adding declarations and functions into 
a new include file called netcdf_compress.h.
The changes have been defined so as to allow the addition of
other compression algorithms.

## Declarations

The compression related extensions are located in the file
*netcdf_compress.h*.

Before presenting the API, it is important to understand how a
compression algorithm is specified to the library.  Basically there are
two parts: (1) the algorithm name and (2) the parameters to the
algorithm (the deflation level for zip, for example).
Note that in order to be compatible with the underlying HDF5 library,
the parameters are defined as a vector of unsigned ints (32-bit).

In light of this, a number of constants are defined.
- NC_COMPRESSION_MAX_NAME -- define the maximum size of the canonical
  name of the compression method: "zip", "bzip2", etc.
- NC_COMPRESSION_MAX_DIMS -- define the max number of dimensions
  that all allowed for any of the compression schemes. This
  is currently less than NC_MAX_VAR_DIMS for implementation
  specific reasons.
- NC_COMPRESSION_MAX_PARAMS -- the maximum number of parameters allowed
  for any algorithm.

For caller convenience, a special union, *nc_compression_t*, is defined in
*netcdf_compress.h* where the arms of the union define parameters for
each algorithm. The file *netcdf_compress.h* should be examined to 
see the definitive declaration of nc_compression_t.

For example, the szip parameters would be defined as
this structure (one arm of *nc_compression_t*).

~~~~~~~~~~~~~~~~~~~~~~~~~
    struct SZIP_PARAMS {
        unsigned int options_mask;
        unsigned int pixels_per_block;
    } szip;
~~~~~~~~~~~~~~~~~~~~~~~~~

Note that this structure contains an integral number of unsigned ints,
two in this case.

Note also that one arm of the *nc_compression_t* union (argv) is just an
array of unsigned ints:

~~~~~~~~~~~~~~~~~~~~~~~~~
union {
unsigned int argv[NC_COMPRESSION_MAX_PARAMS];
...
} nc_compression_t;
~~~~~~~~~~~~~~~~~~~~~~~~~

The process is as follows:

1. assign the parameters to the arm of the union specific to the algorithm.
2. obtain the number of elements in argv (see *nc_algorithm_argc()*).
3. invoke e.g. nc_def_var_compression with the algorithm name, argc,
   and argv as its arguments.

For convenience, and for the currently supported algorithms,
the following nc_compression_t union is defined
It is subject to change as new schemes are added.

~~~~~~~~~~~~~~~~~~~~~~~~~
typedef union {
    unsigned int argv[NC_COMPRESSION_MAX_PARAMS];
    struct ZIP_PARAMS {unsigned int level;} zip;
    struct BZIP2_PARAMS {unsigned int level;} bzip2;
    struct SZIP_PARAMS {
        unsigned int options_mask;
        unsigned int pixels_per_block;
    } szip;
    struct FPZIP_PARAMS {
        int isdouble;
        int precision; /* number of bits of precision (zero = full) */
    } fpzip;
    struct ZFP_PARAMS {
        /*zfp_type*/ int type;
        double rate;
        double tolerance;
        int precision;
    } zfp; 
} nc_compression_t;
~~~~~~~~~~~~~~~~~~~~~~~~~

### Chunking
Every time a compressor (or decompressor) is called,
it is given a block of data comprising one chunk of the variable.
This implies, of course, that any compressed variable must
also have chunking set for it.

The chunks are defined using the *nc_def_var_chunking*
procedure in netcdf.h. So for our variable, v, when compressing,
the compressor will be called with a pointer to a block of data
of size S x c1 x c2 ... x cN where S is the size of a data element
(e.g. 4 for a float) and c1 thru cN are the chunk sizes defined
for the variable.

## API

### nc_def_var_compress

The *nc_def_var_compress* function is a generalization of
*nc_def_var_deflate*.  It sets compression settings for a
variable for any supported compression scheme.  It must be
called after *nc_def_var* and before *nc_enddef*.  The form of
the parameters is, of course, algorithm dependent (see above).

~~~~~~~~~~~~~~~~~~~~~~~~~
int nc_def_var_compress(int ncid,
                        int varid,
                        const char* algorithm,
			size_t argc,
                        unsigned int* argv);
~~~~~~~~~~~~~~~~~~~~~~~~~

The parameter semantics are as follows.

1. *ncid* -- NetCDF or group ID, from a previous call to nc_open(),
nc_create(), nc_def_grp(), or associated inquiry functions such as 
nc_inq_ncid().

2. *varid* -- Variable ID as returned by *nc_def_var*.

3. *algorithm* -- This specifies the name of the compression
algorithm to use (e.g. "zip", "bzip2", etc).

4. *argc* -- The number of elements in the argv argument.

5. *argv* -- This specifies the parameters for the specified
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
                        char* algorithmp,
			size_t* argcp,
                        unsigned int* argv);
~~~~~~~~~~~~~~~~~~~~~~~~~

The parameter semantics are as follows.

1. *ncid* -- NetCDF or group ID, from a previous call to nc_open(),
nc_create(), nc_def_grp(), or associated inquiry functions such as 
nc_inq_ncid().

2. *varid* -- Variable ID

3. *algorithm* -- If this pointer is non-NULL, the name of the compression
algorithm for this variable will be stored into this parameter.
\ref ignored_if_null.

4. *argcp* -- If non-NULL, return the number of unsigned int parameters
in argv.
\ref ignored_if_null.

5. *argv* -- The parameters for the specified
compression algorithm will be stored into this memory.
\ref ignored_if_null.

The possible return codes are as follows.
- *NC_NOERR* -- No error.
- *NC_ENOTNC4* -- Not a netCDF-4 file. 
- *NC_EBADID* -- Bad ncid.
- *NC_ENOTVAR* -- Invalid variable ID.
- *NC_ECOMPRESS* -- Invalid compression parameters.

### nc_def_var_shuffle

In order to simplify the compression related API,
the shuffle flag is now handled like e.g. fletcher32,
namely as a separate set of procedures.

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

### Misc. API Procedures

#### nc_inq_compressor_names

The *nc_inq_compressor_names* procedure
returns the names of all known and active
compression algorithm names.

The term 'active' means that the algorithm
was sucessfully registered with the HDF5 library
and so my be used.

~~~~~~~~~~~~~~~~~~~~~~~~~
const char** nc_inq_algorithm_names(void)
~~~~~~~~~~~~~~~~~~~~~~~~~

This procedure returns a pointer to a vector of pointers to the active
compression algorithm. The vector is NULL terminated (like an argv
vector).  The caller should not attempt to modify or free this return
argument or any part of it. Any internal error (e.g. *malloc* failure)
will cause NULL to be returned.

#### nc_inq_algorithm_argc

The *nc_inq_algorithm_argc* procedure
returns the expected number of elements
in the argv vector of parameters for
the specified algorithm.

~~~~~~~~~~~~~~~~~~~~~~~~~
size_t nc_inq_compressor_(const char* algorithm_name)
~~~~~~~~~~~~~~~~~~~~~~~~~

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
to support variables with N dimensions, where N is greater than 3.

The following scheme has been defined to handle this case.
The scheme is called "Prefix"
For Prefix, The first two dimensions are kept as is, and the
third dimension is increased to cover all of the dimensions
after the first two. So the above is treated as if it was defined
as follows.
~~~~~~~~~~
int v(d1,d2,dM)
~~~~~~~~~~
where dM is defined to have the size d3 x d4 ...x dN.
Since we are actually dealing with chunks, the actual last chunk
is treated as if it was of size c3 x d4 ...x cN.
<!--
2. Choice (M out of N) -- The idea here is to define the chunks so that
   at most three have chunk sizes greater than one. This potentially
   has significant consequences for performance because it can increase
   the number of chunks dramatically, which can significantly increase
   compression time.

The choice of scheme is determined as follows.

1. If all but three of the chunksizes are set to 1 (one),
   then the choice scheme will be used.
2. Otherwise, prefix will be used.
-->

<!--
# Adding a New Compression Algorithm

Adding a new compression algorithm is of medium complexity. But it is
important to understand how HDF5 handles compression filters. Please
read and understand the following documents:
- [HDF5 Dynamically Loaded Filters](https://www.hdfgroup.org/HDF5/doc/Advanced/DynamicallyLoadedFilters/HDF5DynamicallyLoadedFilters.pdf)
- [H5Z: Filter and Compression Interface](https://www.hdfgroup.org/HDF5/doc/RM/RM_H5Z.html)

Suppose we would like to add a new compression algorithm called *xpress*.
The first steps are as follows.

1. Define a string name for the new algorithm, presumably "xpress"
   in this case.

2. Define a struct to add to the *nc_compression_t* union.
   For Example, it might be like this.
~~~~~~~~~~
struct XPRESS_PARAMS {
 T1 param1;
 T2 param2;
} xpress;
~~~~~~~~~~
   where T1 and T2 are some defined type (e.g. int or float).

3. Define a corresponding struct for 

Note that at some point in the future, and attempt may be made
to support dynamic (at run-time) addition of compressors.
In this case, serious internal changes will probably occur.
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
2. The library for each of the compression algorithms must be accessible,
   typically thru the *LDFLAGS* and *CPPFLAGS* environment variables.

If *--with-compression* is not specified, then it defaults to zip only.
Using --with-compression=all will cause a default set of compression
algorithms to be used.

## Cmake
1. The flag *-DWITH_COMPRESSION=c1,c2,...cn*
   must be used and the name of the desired compression algorithm
   (e.g. bzip2) must appear as one of the comma separated ci.
2. The library for the compression algorithm must be accessible.
   Typically, the flag *-DCMAKE_PREFIX_PATH=...* is used
   to specify these libraries.

If *-DWITH_COMPRESSION* is not specified, then it defaults to zip only.
Using -DWITH_COMPRESSION=all will cause a default set of compression
algorithms to be used.

