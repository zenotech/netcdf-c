#ifndef NC4COMPRESS_H
#define NC4COMPRESS_H

#include <hdf5.h>
#ifdef FPZIP_FILTER
#include "fpzip.h"
#endif
#ifdef ZFP_FILTER
#include "zfp.h"
#endif
#include "netcdf_compress.h"

/* These must all be <= NC_COMPRESSION_MAX_PARAMS */
#define NC_NELEMS_ZIP 1
#define NC_NELEMS_BZIP2 1
#define NC_NELEMS_SZIP 2 /* use only 2 of 4 */
#define NC_NELEMS_FPZIP 8
#define NC_NELEMS_ZFP 18

/* This must be the >= max of the NC_NELEMS_XXX in nc4compress.h */
#define NC_COMPRESSION_MAX_PARAMS 32

/* Define an "enum" for all potentially supported compressors;
   The max number of compressors is 127 to fit into signed char.
*/
typedef enum NC_algorithm {
NC_NOZIP = 0,/*must be 0*/
NC_ZIP   = 1,
NC_SZIP  = 2,
NC_BZIP2 = 3,
NC_FPZIP = 4,
NC_ZFP   = 5,
NC_COMPRESSORS = (NC_ZFP+1)
} NC_algorithm;

/* This struct defines the actual
   set of parameters given to HDF5
   plus per-variable extra data.
   It is computed from nc_compression_t instance.
   Each arm must be fixed size and must be a multiple
   of sizeof(unsigned int) in size.
*/

typedef struct NC_compression_info {
    NC_algorithm algorithm;
    size_t argc; /* Number of unsigned ints per algorithm */
    union {/* Per-algorithm arms */
        unsigned int argv[NC_COMPRESSION_MAX_PARAMS];/*arbitrary 32 bit values*/
        struct ZIPINFO {unsigned int level;} zip;
        struct BZIP2INFO {unsigned int level;} bzip2;
        struct SZIPINFO {
            unsigned int options_mask;
            unsigned int pixels_per_block;
        } szip;
        struct FPZIPINFO {
#ifdef FPZIP_FILTER
            FPZ fpz;
#endif
	    unsigned long long totalsize; /* chunksizes cross product */
        } fpzip;
        struct ZFPINFO {
	    struct ZFP_PARAMS params; /* From nc_compresssion_t */
	    unsigned int nx, ny, nz;
	    unsigned long long totalsize; /* chunksizes cross product */
	    int rank;
	    unsigned long long zfp_params; /* concise representation of zfp params of interest*/
        } zfp;
    } params;
} NC_compression_info;

/*
Turn on specified compression for a variable (via plist)
*/
EXTERNL int NC_compress_set(NC_compression_info*, hid_t plistid, int rank, size_t* chunksizes);

/*
Turn on shuffle for a variable (via plist)
*/
EXTERNL int NC_compress_shuffle(hid_t plistid, int);

#if 0
/*
Get the compression parameters from file variable into NC_VAR_INFO_T instance
*/
EXTERNL int NC_compress_inq_argv(hid_t h5id, int filterindex, NC_algorithm*, int* nelemsp, unsigned int* elems);
#endif

/*
Validate a set of compression parameters
*/
EXTERNL int NC_compress_validate(NC_compression_info* info);

/*
Convert an HDF5 filter id into a name or null
*/
EXTERNL const char* NC_compress_name_for(int id);

/* 
Register all compression filters with the library
*/
EXTERNL int NC_compress_register_all(void);

EXTERNL NC_algorithm NC_algorithm_id(const char* name);
EXTERNL const char* NC_algorithm_name(NC_algorithm id);
EXTERNL NC_algorithm NC_algorithm_for_filter(H5Z_filter_t h5id);
EXTERNL size_t NC_algorithm_nelems(NC_algorithm);

/* Convert NC_compression_info -> arm of NC_compression_t */
EXTERNL int NC_compress_cvt_from(NC_compression_info*,void*);
/* Convert arm of NC_compression_t -> NC_compression_info */
EXTERNL int NC_compress_cvt_to(NC_algorithm alg, void*, NC_compression_info*);

#endif /*NC4COMPRESS_H*/
