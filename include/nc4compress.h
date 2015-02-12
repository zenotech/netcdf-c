#ifndef NC4COMPRESS_H
#define NC4COMPRESS_H

#include <hdf5.h>
#ifdef FPZIP_FILTER
#include <fpzip.h>
#endif

/* These must all be <= NC_COMPRESSION_MAX_PARAMS in netcdf.h */
#define NC_NELEMS_ZIP 1
#define NC_NELEMS_SZIP 2 /* use only 2 of 4 */
#define NC_NELEMS_BZIP2 1
#define NC_NELEMS_FPZIP 36
#define NC_NELEMS_ZFP 42

/**
Currently supported algorithms
for dealing with M dimensions, M > N.
Applies to fpzip and zfp currently.
"prefix" - use the first N-1 dimensions
   and make dim N be the cross product of
   all remaining (M-N+1) dimensions.
"suffix" - use the last N-1 dimensions
   and repeatedly apply the algorithm
   to the last N dimensions.
"choose" - require all but N of the chunks
   have a value of 1.
*/
#define NC_NDIM_PREFIX 'p'
#define NC_NDIM_SUFFIX 's'
#define NC_NDIM_CHOOSE 'c'

/* It should be possible to overlay this
on the params[] to extract the values.
This should match the union comment in netcdf.h.
Note that all fields should be fixed size.
*/
typedef union {
    unsigned int params[NC_COMPRESSION_MAX_PARAMS];/*arbitrary 32 bit values*/
    struct {unsigned int level;} zip;
    struct {unsigned int level;} bzip2;
    struct {
        unsigned int options_mask;
        unsigned int pixels_per_block;
    } szip;
    struct {
	int isdouble;
	int prec; /* number of bits of precision (zero = full) */
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

/*
Turn on specified compression for a variable (via plist)
*/
EXTERNL int nc_compress_set(const char* algorithm, hid_t plistid, int, unsigned int*);

/*
Turn on shuffle for a variable (via plist)
*/
EXTERNL int nc_compress_shuffle(hid_t plistid, int);

/*
Get the compression parameters for a variable
*/
EXTERNL int nc_compress_inq_parameters(const char*, hid_t, int, unsigned int*,char*,int*,unsigned int*);

/*
Validate a set of compression parameters
*/
EXTERNL int nc_compress_validate(const char*, int, unsigned int*);

/*
Convert an HDF5 filter id into a name or null
*/
EXTERNL const char* nc_compress_name_for(int id);

/* 
Register all compression filters with the library
*/
EXTERNL int nc_compress_register_all(void);


#endif /*NC4COMPRESS_H*/
