#ifndef NETCDF_COMPRESS_H
#define NETCDF_COMPRESS_H 1

/* Define the max size of a compression alg name; 16 - 1 for trailing null */
#define NC_COMPRESSION_MAX_NAME 15

/* Define the max number of dimensions that can be handled by
   some of the compressors */
#define NC_COMPRESSION_MAX_DIMS 16

/* Compression max/min for simple deflates */
#define NC_DEFLATE_LEVEL_MIN 0
#define NC_DEFLATE_LEVEL_MAX 9

/*
It should be possible to cast void* to this
to extract the per-algorithm parameters.
*/
typedef union {
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

/* Set compression settings for a variable.
   Must be called after nc_def_var and before nc_enddef.
   The form of the parameters is algorithm dependent (see above union)
*/
EXTERNL int
nc_def_var_compress(int ncid, int varid, const char* algorithm, void* params);

/* Find out compression settings of a var. */
EXTERNL int
nc_inq_var_compress(int ncid, int varid, char* algorithm, void* params);

/* Define the shuffle of a variable. */
EXTERNL int
nc_def_var_shuffle(int ncid, int varid, int shuffle);

/* Learn about the shuffle of a variable. */
EXTERNL int
nc_inq_var_shuffle(int ncid, int varid, int *shufflep);

#endif /*NETCDF_COMPRESS_H*/
