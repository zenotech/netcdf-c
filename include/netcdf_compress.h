#ifndef NETCDF_COMPRESS_H
#define NETCDF_COMPRESS_H 1

/* Define the max size of a compression alg name; 16 - 1 for trailing null */
#define NC_COMPRESSION_MAX_NAME 15
/* This must be the max of the NC_NELEMS_XXX in nc4compress.h */
#define NC_COMPRESSION_MAX_PARAMS 64
/* Define the max number of dimensions that can be handled by
   some of the compressors */
#define NC_COMPRESSION_MAX_DIMS 16
/* Compression max/min for simple deflates */
#define NC_DEFLATE_LEVEL_MIN 0
#define NC_DEFLATE_LEVEL_MAX 9

/* These must all be <= NC_COMPRESSION_MAX_PARAMS in netcdf.h */
#define NC_NELEMS_ZIP 1
#define NC_NELEMS_SZIP 2 /* use only 2 of 4 */
#define NC_NELEMS_BZIP2 1
#define NC_NELEMS_FPZIP 36
#define NC_NELEMS_ZFP 39

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
        /*zfp_type*/ int type;
        double rate;
        double tolerance;
	int precision;
        int rank;
	size_t chunksizes[NC_COMPRESSION_MAX_DIMS];
    } zfp; 
} nc_compression_t;

/** 
The compression parameters are stored in an
array of unsigned ints. For the current set of algorithms,
the array conforms to the union defined above
*/

/* Set compression settings for a variable.
   Must be called after nc_def_var and before nc_enddef.
   The form of the parameters is algorithm dependent.
*/
EXTERNL int
nc_def_var_compress(int ncid, int varid, const char* algorithm, int nparams, unsigned int* params);

/* Find out compression settings of a var. */
EXTERNL int
nc_inq_var_compress(int ncid, int varid,
		    char**algorithmp, int* nparamsp, unsigned int* paramsp);

#endif /*NETCDF_COMPRESS_H*/
