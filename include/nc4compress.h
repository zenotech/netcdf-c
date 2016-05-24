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
NC_NCOMPRESSORS = (NC_ZFP+1)
} NC_algorithm;

/* These must all be <= NC_COMPRESSION_MAX_PARAMS */
#define NC_NELEMS_ZIP 1
#define NC_NELEMS_BZIP2 1
#define NC_NELEMS_SZIP 2 /* use only 2 of 4 */
#define NC_NELEMS_FPZIP 8
#define NC_NELEMS_ZFP 18

/* This struct is the internal dual of nc_compression_t.
   It defines the actual parameters given to HDF5.
   It is computed from nc_compression_t instance.
   As with nc_compression_t, each arm must be fixed size
   and must be a multiple of sizeof(unsigned int) in size.
*/

typedef struct NC_compression_info {
    NC_algorithm algorithm;
    size_t argc; /* Number of unsigned ints per algorithm */
    nc_compression_t params;
} NC_compression_info;

/* The reserved parts of nc_compression_t conform to these structures */

struct fpzip_reserved {
    int nx;
    int ny;
    int nz;
    int nf;	
    unsigned long long totalsize; /* chunksizes cross product */
};

struct zfp_reserved {
    unsigned int rank, nx, ny, nz;
    unsigned long long totalsize; /* chunksizes cross product */
    unsigned long long zfp_params; /* concise representation of zfp params of interest*/
};

/*
Turn on specified compression for a variable (via plist)
*/
EXTERNL int NC_compress_set(NC_compression_info*, hid_t plistid, int rank, size_t* chunksizes);

/*
Turn on shuffle for a variable (via plist)
*/
EXTERNL int NC_compress_shuffle(hid_t plistid, int);

/*
Validate a set of compression parameters
*/
EXTERNL int NC_compress_validate(NC_compression_info* info);

/*
Convert an NC_algorithm id into a name or null
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
EXTERNL int NC_compress_cvt_from(NC_compression_info*,size_t,unsigned int*);
/* Convert arm of NC_compression_t -> NC_compression_info */
EXTERNL int NC_compress_cvt_to(NC_algorithm alg, size_t, unsigned int*, NC_compression_info*);

#endif /*NC4COMPRESS_H*/
