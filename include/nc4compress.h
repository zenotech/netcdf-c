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
#define NC_NELEMS_JP2 100

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
    struct {
	struct jp2_compress {
           int cp_reduce;
           int cp_layer;
           int decod_format;
           int cod_format;
           OPJ_LIMIT_DECODING cp_limit_decoding;
	} decompress;
	struct jp2_compress {
           bool tile_size_on;
           int cp_tx0;
           int cp_ty0;
           int cp_tdx;
           int cp_tdy;
           int cp_disto_alloc;
           int cp_fixed_alloc;
           int cp_fixed_quality;
           int *cp_matrice;
           int csty;
           OPJ_PROG_ORDER prog_order;
           opj_poc_t POC[32];
           int numpocs;
           int tcp_numlayers;
           float tcp_rates[100];
           float tcp_distoratio[100];
           int numresolution;
           int cblockw_init;
           int cblockh_init;
           int irreversible;
           int roi_compno;
           int roi_shift;
           int res_spec;
           int prcw_init[J2K_MAXRLVLS];
           int prch_init[J2K_MAXRLVLS];
	} compress;
        int rank;
	size_t chunksizes[NC_COMPRESSION_MAX_DIMS];
    } jp2;
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
