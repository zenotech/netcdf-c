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

/* Defined in hdf5 headers */
#if 0
#define H5Z_FILTER_DEFLATE
#define H5Z_FILTER_SZIP
#endif

#define H5Z_FILTER_BZIP2 307 /*Already defined by hdf group */

/* These use the unassigned id's made available by the hdfgroup */
/* See https://www.hdfgroup.org/HDF5/doc/H5.user/Filters.html */
/* At some point, these algorithms need to be assigned by hdfgroup */
#define H5Z_FILTER_FPZIP 256
#define H5Z_FILTER_ZFP 257

#define H5Z_FILTER_NOZIP 0 /*must be 0*/

#define NC_NCOMPRESSORS 5

/* These must all be <= NC_COMPRESSION_MAX_PARAMS */
#define NC_NELEMS_ZIP 1
#define NC_NELEMS_BZIP2 1
#define NC_NELEMS_SZIP 2 /* use only 2 of 4 */
#define NC_NELEMS_FPZIP 8
#define NC_NELEMS_ZFP 11

/*
Turn on specified compression for a variable (via plist)
*/
extern int NC_compress_set(H5Z_filter_t,size_t,unsigned int*, hid_t plistid, int rank, size_t* chunksizes);

/*
Turn on shuffle for a variable (via plist)
*/
extern int NC_compress_shuffle(hid_t plistid, int);

/*
Convert an H5Z_filter_t id into a name or null
*/
extern const char* NC_compress_name_for(H5Z_filter_t id);

/* 
Register all compression filters with the library
*/
extern int NC_compress_register_all(void);

extern H5Z_filter_t NC_algorithm_id(const char* name);
extern const char* NC_algorithm_name(H5Z_filter_t id);
extern size_t NC_algorithm_nelems(H5Z_filter_t);

#endif /*NC4COMPRESS_H*/
