#ifndef NETCDF_COMPRESS_H
#define NETCDF_COMPRESS_H 1

/* Define the max size of a compression alg name; 16 - 1 for trailing null */
#define NC_COMPRESSION_MAX_NAME 15

/* Define the max number of dimensions that can be handled by
   some of the compressors */
#define NC_COMPRESSION_MAX_DIMS 16

/* Max number of parameters allowed for any algorithm */
#define NC_COMPRESSION_MAX_PARAMS 64

/* Compression max/min for simple deflates */
#define NC_DEFLATE_LEVEL_MIN 0
#define NC_DEFLATE_LEVEL_MAX 9

typedef union {
    unsigned int argv[NC_COMPRESSION_MAX_PARAMS];
    struct zip_params {unsigned int level;} zip;
    struct bzip2_params {unsigned int level;} bzip2;
    struct szip_params {
        unsigned int options_mask;
        unsigned int pixels_per_block;
    } szip;
    struct fpzip_params {
	int type; /* FPZIP_TYPE_FLOAT | FPZIP_TYPE_DOUBLE */
	int precision; /* number of bits of precision (zero = full) */
	/* Reserved for internal use: subject to change */
	unsigned int reserved[6];
    } fpzip;
    struct zfp_params {
        int type; /*zfp_type*/
        double rate;
        double tolerance;
	int precision;
	/* Reserved for internal use: subject to change */
	unsigned int reserved[8];
    } zfp; 
} nc_compression_t;

/* Set compression settings for a variable.
   Must be called after nc_def_var and before nc_enddef.
   The form of the parameters is algorithm dependent (see above union)
*/
EXTERNL int
nc_def_var_compress(int ncid, int varid, const char* algorithm, size_t argc, unsigned int* argv);

/* Find out compression settings of a var. */
EXTERNL int
nc_inq_var_compress(int ncid, int varid, char* algorithm, size_t* argcp, unsigned int* argv);

/* Define the shuffle of a variable. */
EXTERNL int
nc_def_var_shuffle(int ncid, int varid, int shuffle);

/* Learn about the shuffle of a variable. */
EXTERNL int
nc_inq_var_shuffle(int ncid, int varid, int *shufflep);

/* Get set of known algorithms by name; result
   is NULL terminated; do not free
*/
EXTERNL const char**
nc_inq_algorithm_names(void);

/* Get the argc for a given algorithm */
EXTERNL size_t
nc_inq_algorithm_argc(const char* algorithm);

#endif /*NETCDF_COMPRESS_H*/
