#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef SZIP_FILTER
#include <szlib.h>
#endif
#ifdef BZIP2_FILTER
#include <bzlib.h>
#endif
#ifdef FPZIP_FILTER
#include <fpzip.h>
#endif
#ifdef ZFP_FILTER
#include <zfp.h>
#endif


/*
Built in to libhdf5
H5Z_FILTER_DEFLATE
H5Z_FILTER_SZIP
*/

#ifndef SZIP_FILTER
#define SZ_MAX_PIXELS_PER_BLOCK 0
#endif

#include "netcdf.h"
#include "hdf5.h"
#include "nc4compress.h"
#include "nc_logging.h"

#define DEBUG

#define VERIFYSIZE 1

/* From hdf5.H5private.h */
#define H5_ASSIGN_OVERFLOW(dst, src, srctype, dsttype)  \
    (dst) = (dsttype)(src);
#define H5_CHECK_OVERFLOW(var, vartype, casttype)

/* From hdf5.H5Fprivate.h */
#  define UINT32DECODE(p, i) {						      \
   (i)	=  (uint32_t)(*(p) & 0xff);	   (p)++;			      \
   (i) |= ((uint32_t)(*(p) & 0xff) <<  8); (p)++;			      \
   (i) |= ((uint32_t)(*(p) & 0xff) << 16); (p)++;			      \
   (i) |= ((uint32_t)(*(p) & 0xff) << 24); (p)++;			      \
}
#  define UINT32ENCODE(p, i) {						      \
   *(p) = (uint8_t)( (i)        & 0xff); (p)++;				      \
   *(p) = (uint8_t)(((i) >>  8) & 0xff); (p)++;				      \
   *(p) = (uint8_t)(((i) >> 16) & 0xff); (p)++;				      \
   *(p) = (uint8_t)(((i) >> 24) & 0xff); (p)++;				      \
}

typedef struct NCC_COMPRESSOR {
    H5Z_filter_t filterid; /* as assigned/chosen from HDF5 id space */
    char name[NC_COMPRESSION_MAX_NAME+1]; /* canonical compressor name */
    size_t nelems; /* size of the compression parameters in units of uint32 */
    int registered;
    H5Z_func_t h5filterfunc;
    int (*_init)(size_t nelems, unsigned int* params, int rank, size_t* chunksizes);
} NCC_COMPRESSOR;

#if 0
    H5Z_class2_t* h5info;
#endif

/* The reserved parts of nc_compression_t conform to these structures */

struct fpzip_reserved {
    int nx;
    int ny;
    int nz;
    int nf;	
    unsigned long long crossproduct; /* chunksizes cross product */
};

struct zfp_reserved {
    unsigned int rank, nx, ny, nz;
    unsigned long long crossproduct; /* chunksizes cross product */
};

static int NC_compress_initialized = 0;

/*Forward*/
static int zip_init(size_t,unsigned int*,int,size_t*);
static int szip_init(size_t,unsigned int*,int,size_t*);
static int bzip2_init(size_t,unsigned int*,int,size_t*);
static int fpzip_init(size_t,unsigned int*,int,size_t*);
static int zfp_init(size_t,unsigned int*,int,size_t*);

static size_t H5Z_filter_bzip2(unsigned,size_t,const unsigned[],size_t,size_t*,void**);
static size_t H5Z_filter_fpzip(unsigned,size_t,const unsigned[],size_t,size_t*,void**);
static size_t H5Z_filter_zfp(unsigned,size_t,const unsigned[],size_t,size_t*,void**);

#ifndef DEBUG
#define THROW(e) (e)
#else
#define THROW(e) cmpbreak(e,__FILE__,__LINE__)
static int
cmpbreak(int e, const char* file, int line)
{
   if(e != 0) {
     fprintf(stderr, "Error %d in file %s, line %d.\n", e, file, line);
     fflush(stderr);
   }
   return e;
}
#endif

/* Forward */
static int available(NCC_COMPRESSOR* info, H5Z_class2_t*);
static NCC_COMPRESSOR compressors[NC_NCOMPRESSORS+1];
static NCC_COMPRESSOR* NC_compressor_for(H5Z_filter_t index);
static void verifysize();

/*
Turn on compression for a variable's plist
*/
int
NC_compress_set(H5Z_filter_t alg, size_t argc, unsigned int* argv, hid_t vid, int rank, size_t* chunksizes)
{
    NCC_COMPRESSOR* cmp = NC_compressor_for(alg);
    int stat = NC_NOERR;
    herr_t hstat = 0;
    if(cmp == NULL)
        LOG((1,"%s: error: unsupported compression: %d",
	    __func__, alg));
    if(!cmp->registered)
        LOG((1,"%s: error: unregistered compression: %s",
	    __func__, cmp->name));

    if(argc < cmp->nelems) {
	LOG((1,"%s: argc mismatch: %d::%d",cmp->name,argc,cmp->nelems));
	return NC_ECOMPRESS;
    }

    /* Complete parameter initialization */
    stat = cmp->_init(argc,argv,rank,chunksizes);
    /* Attach the compressor to the variable */
    if(stat == NC_NOERR) {
        hstat = H5Pset_filter(vid, cmp->filterid, H5Z_FLAG_MANDATORY, argc, argv);
 	if(hstat != 0)
	    stat = NC_ECOMPRESS;
    }
    return THROW(stat);
}

/* 
Register all known filters with the library
*/
int
NC_compress_register_all(void)
{
    int stat = NC_NOERR;
    herr_t hstat = 0;
    NCC_COMPRESSOR* cmp;
    if(NC_compress_initialized == 1) return NC_NOERR;
    NC_compress_initialized = 1; /* avoid repeats */
#ifdef VERIFYSIZE
    verifysize();
#endif
    for(cmp=compressors;cmp->filterid != H5Z_FILTER_NOZIP;cmp++) {
        H5Z_class2_t h5info;
	h5info.version = H5Z_CLASS_T_VERS;
	h5info.encoder_present = 1;
	h5info.decoder_present = 1;
	h5info.can_apply = NULL;
	h5info.set_local = NULL;
	h5info.name = cmp->name;
	h5info.id = cmp->filterid;
	h5info.filter = cmp->h5filterfunc;
	/* two special cases */
	hstat = 0;
	if(cmp->filterid != H5Z_FILTER_DEFLATE && cmp->filterid != H5Z_FILTER_SZIP)
	    hstat = H5Zregister(&h5info);
	if(hstat != 0)
	    stat = NC_ECOMPRESS;
	else {
	    cmp->registered = 1;
            if(available(cmp,&h5info) != NC_NOERR) {
		LOG((3,"Filter not available for encoding and decoding: %s.\n",cmp->name));
	    }
	}
    }
    return THROW(NC_NOERR);
}


/*
* Check if compression is available and can be used for both
* compression and decompression.  Normally we do not perform error
* checking in these examples for the sake of clarity, but in this
* case we will make an exception because this filter is an
* optional part of the hdf5 library.
*/
static int
available(NCC_COMPRESSOR* info, H5Z_class2_t* h5info)
{
    htri_t avail;
    unsigned int filter_info;

    if(info->registered) {
        avail = H5Zfilter_avail(h5info->id);
        if(!avail) {
            LOG((3,"Filter not available: %s.\n",info->name));
            return NC_ECOMPRESS;
	}
        if(H5Zget_filter_info(h5info->id, &filter_info))
	    return THROW(NC_ECOMPRESS);
        if(!(filter_info & H5Z_FILTER_CONFIG_ENCODE_ENABLED)
           || !(filter_info & H5Z_FILTER_CONFIG_DECODE_ENABLED) ) {
            return THROW(NC_ECOMPRESS);
	}
    }
    return THROW(NC_NOERR);
}

/**************************************************/
/*#ifdef ZIP (DEFLATE) compression always defined */

static int
zip_init(size_t argc, unsigned int* argv, int rank, size_t* chunksizes)
{
    int stat = NC_NOERR;
    nc_compression_t* info = (nc_compression_t*)argv;

    if(argc < NC_NELEMS_ZIP) {
	LOG((1,"zip: argc mismatch: %d::%d",argc,NC_NELEMS_ZIP));
	return NC_ECOMPRESS;
    }
    if(rank < 0 || rank > NC_COMPRESSION_MAX_DIMS) {
	LOG((1,"zip: rank too large: %d",rank));
	return NC_EINVAL;
    }
    /* validate level */
    if(info->zip.level < NC_DEFLATE_LEVEL_MIN ||
	info->zip.level > NC_DEFLATE_LEVEL_MAX) {
	LOG((1,"zip: level out of range:: %d",info->zip.level));
	return NC_EINVAL;
    }
    return THROW(stat);
}

/**************************************************/

static int
szip_init(size_t argc, unsigned int* argv, int rank, size_t* chunksizes)
{
    int stat = NC_NOERR;
    nc_compression_t* info = (nc_compression_t*)argv;
    if(argc < NC_NELEMS_SZIP) {
	LOG((1,"szip: argc mismatch: %d::%d",argc,NC_NELEMS_SZIP));
	return NC_ECOMPRESS;
    }
    if(rank < 0 || rank > NC_COMPRESSION_MAX_DIMS) {
	LOG((1,"szip: rank too large: %d",rank));
	return NC_EINVAL;
    }
    /* validate bpp */
    if(info->szip.pixels_per_block > SZ_MAX_PIXELS_PER_BLOCK) {
	LOG((1,"szip: pixels/block out of  range:: %d",info->szip.pixels_per_block));
	return NC_EINVAL;
    }
    return THROW(stat);
}

/**************************************************/

static int
bzip2_init(size_t argc, unsigned int* argv, int rank, size_t* chunksizes)
{
    int stat = NC_NOERR;
    nc_compression_t* info = (nc_compression_t*)argv;
    if(argc < NC_NELEMS_BZIP2) {
	LOG((1,"bzip2: argc mismatch: %d::%d",argc,NC_NELEMS_BZIP2));
	return NC_ECOMPRESS;
    }
    if(rank < 0 || rank > NC_COMPRESSION_MAX_DIMS) {
	LOG((1,"bzip2: rank too large: %d",rank));
	return NC_EINVAL;
    }
    if(info->bzip2.level < NC_DEFLATE_LEVEL_MIN ||
       info->bzip2.level > NC_DEFLATE_LEVEL_MAX) {
	LOG((1,"bzip2: level out of range:: %d",info->bzip2.level));
	return NC_EINVAL;
    }
    return THROW(stat);
}

/*
1. Each incoming block represents one complete chunks
*/
static size_t
H5Z_filter_bzip2(unsigned int flags, size_t cd_nelmts,
                     const unsigned int argv[], size_t nbytes,
                     size_t *buf_size, void **buf)
{
    int ret;
    char *outbuf = NULL;
    size_t outbuflen, outdatalen;
    struct bzip2_params* binfo = NULL;

    if(nbytes == 0) return 0; /* sanity check */

    binfo = (struct bzip2_params*)argv;

    if(flags & H5Z_FLAG_REVERSE) {
  
	/** Decompress data.
         **
         ** This process is troublesome since the size of uncompressed data
         ** is unknown, so the low-level interface must be used.
         ** Data is decompressed to the output buffer (which is sized
         ** for the average case); if it gets full, its size is doubled
         ** and decompression continues.  This avoids repeatedly trying to
         ** decompress the whole block, which could be really inefficient.
         **/
  
	bz_stream stream;
	char *newbuf = NULL;
	size_t newbuflen;
  
        /* Prepare the output buffer. */
        outbuflen = nbytes * 3 + 1;/* average bzip2 compression ratio is 3:1 */
        outbuf = malloc(outbuflen);
	if(outbuf == NULL) {
	    fprintf(stderr,"memory allocation failed for bzip2 decompression\n");
	    goto cleanupAndFail;
	}
        /* Use standard malloc()/free() for internal memory handling. */
        stream.bzalloc = NULL;
        stream.bzfree = NULL;
        stream.opaque = NULL;

        /* Start decompression. */
        ret = BZ2_bzDecompressInit(&stream, 0, 0);
        if(ret != BZ_OK) {
            fprintf(stderr, "bzip2 decompression start failed with error %d\n", ret);
            goto cleanupAndFail;
	}

        /* Feed data to the decompression process and get decompressed data. */
        stream.next_out = outbuf;
        stream.avail_out = outbuflen;
        stream.next_in = *buf;
        stream.avail_in = nbytes;
        do {
	    ret = BZ2_bzDecompress(&stream);
            if(ret < 0) {
                fprintf(stderr, "BUG: bzip2 decompression failed with error %d\n", ret);
                goto cleanupAndFail;
            }
            if(ret != BZ_STREAM_END && stream.avail_out == 0) {
                /* Grow the output buffer. */
                newbuflen = outbuflen * 2;
                newbuf = realloc(outbuf, newbuflen);
                if(newbuf == NULL) {
                    fprintf(stderr, "memory allocation failed for bzip2 decompression\n");
                    goto cleanupAndFail;
                }
                stream.next_out = newbuf + outbuflen;  /* half the new buffer behind */
                stream.avail_out = outbuflen;  /* half the new buffer ahead */
                outbuf = newbuf;
                outbuflen = newbuflen;
            }
        } while (ret != BZ_STREAM_END);

        /* End compression. */
        outdatalen = stream.total_out_lo32;
        ret = BZ2_bzDecompressEnd(&stream);
        if(ret != BZ_OK) {
            fprintf(stderr, "bzip2 compression end failed with error %d\n", ret);
            goto cleanupAndFail;
        }
    } else {

	/** Compress data.
         **
         ** This is quite simple, since the size of compressed data in the worst
         ** case is known and it is not much bigger than the size of uncompressed
         ** data.  This allows us to use the simplified one-shot interface to
         ** compression.
         **/
   
	unsigned int odatalen;  /* maybe not the same size as outdatalen */
        int blockSize100k = 9;
   
        /* Get compression block size if present. */
	if(cd_nelmts > 0) {
            blockSize100k = argv[0];
	    if(blockSize100k < 1 || blockSize100k > 9) {
		fprintf(stderr, "invalid compression block size: %d\n", blockSize100k);
                goto cleanupAndFail;
	    }
        }
    
        /* Prepare the output buffer. */
        outbuflen = nbytes + nbytes / 100 + 600;  /* worst case (bzip2 docs) */
        outbuf = malloc(outbuflen);
        if(outbuf == NULL) {
	    fprintf(stderr, "memory allocation failed for bzip2 compression\n");
            goto cleanupAndFail;
        }
    
        /* Compress data. */
        odatalen = outbuflen;
        ret = BZ2_bzBuffToBuffCompress(outbuf, &odatalen, *buf, nbytes,
                                       blockSize100k, 0, 0);
        outdatalen = odatalen;
        if(ret != BZ_OK) {
	    fprintf(stderr, "bzip2 compression failed with error %d\n", ret);
            goto cleanupAndFail;
        }
    }

    /* Always replace the input buffer with the output buffer. */
    free(*buf);
    *buf = outbuf;
    *buf_size = outbuflen;
    return outdatalen;
    
cleanupAndFail:
    if(outbuf)
        free(outbuf);
    return 0;
}
    
/**************************************************/
    
static int
fpzip_valid(nc_compression_t* info, int rank, size_t* chunksizes)
{
    int stat = NC_NOERR;
    if(rank < 0 || rank > NC_COMPRESSION_MAX_DIMS) {
	LOG((1,"zip: rank too large: %d",rank));
	return NC_EINVAL;
    }
    if(info->fpzip.precision < 0 || info->fpzip.precision > 64) {
	LOG((1,"fpzip: precision out of range:: %d",info->fpzip.precision));
	return NC_EINVAL;
    }
    if(info->fpzip.precision > 32
       && info->fpzip.type != FPZIP_TYPE_DOUBLE) {
	LOG((1,"fpzip: precision::type mismatch: %d",info->fpzip.precision));
	return NC_EINVAL;
    }
    return THROW(stat);
}

#ifdef FPZIP_FILTER

static int
fpzip_init(size_t argc, unsigned int* argv, int rank, size_t* chunksizes)
{
    int stat = NC_NOERR;
    nc_compression_t* info = (nc_compression_t*)argv;
    int choice,gtone,i,isdouble;
    size_t crossproduct,elemsize;
    size_t nx,ny,nz,nf,nzsize;
    struct fpzip_params* fpz = &info->fpzip;
    struct fpzip_reserved* resv = (struct fpzip_reserved*)fpz->reserved;

    if(argc < NC_NELEMS_FPZIP) {
	LOG((1,"fpzip: argc mismatch: %d::%d",argc,NC_NELEMS_FPZIP));
	return NC_ECOMPRESS;
    }

    /* Push off validation */
    stat = fpzip_valid(info,rank,chunksizes);
    if(stat != NC_NOERR)
	goto done;

    for(gtone=0,crossproduct=1,i=0;i<rank;i++) {
	crossproduct *= chunksizes[i];
	if(chunksizes[i] > 1) gtone++;
    }

#ifdef CHOICE
    choice = (gtone == 3 && rank > 3 ? 1 : 0);
#else
    choice = 0;
#endif

    if(choice) {
        nx = ny = nz = nf = 1;
        for(i=0;i<rank;i++) {
	    if(chunksizes[i] > 1) {
  	        if(nx == 1) nx = chunksizes[i];
	        else if(ny == 1) ny = chunksizes[i];
	        else if(nz == 1) nz = chunksizes[i];
	    }
	}
    } else {/*prefix*/
        /* Do some computations */
        nzsize = 0;
        if(rank > 2) {
            for(nzsize=1,i=2;i<rank;i++)
	        nzsize *= chunksizes[i];
	}
    }

    isdouble = (fpz->type == FPZIP_TYPE_DOUBLE);

    /* Element size (in bytes) */
    elemsize = (isdouble ? sizeof(double) : sizeof(float));

    /* precision */
    if(fpz->precision == 0)
        fpz->precision = CHAR_BIT * elemsize;

    if(choice) {
	resv->nx = nx;
	resv->ny = ny;
	resv->nz = nz;
	resv->nf = nf;
    } else {/*prefix*/
	resv->nx = chunksizes[0];
	resv->ny = (rank >= 2 ? chunksizes[1] : 1);
	resv->nz = (rank >= 3 ? nzsize : 1);
	resv->nf = 1;
    }
    resv->crossproduct = crossproduct;
done:
    return THROW(stat);
}

/**
Assumptions:
1. Each incoming block represents one complete chunks
2. If "choose" is enabled, then only 3 chunks can have
   value different from 1 (one).
*/
static size_t
H5Z_filter_fpzip(unsigned int flags, size_t cd_nelmts,
                     const unsigned int argv[], size_t nbytes,
                     size_t *buf_size, void **buf)
{
    int i;
    int rank;
    int isdouble;
    int prec;
    char *outbuf = NULL;
    size_t databytes;
    size_t elemsize;
    size_t crossproduct;
    size_t outbuf_used = 0;
    FPZ* fpz = NULL;
    struct fpzip_params* finfo;
    struct fpzip_reserved* resv;

    if(nbytes == 0) return 0; /* sanity check */

    finfo = (struct fpzip_params*)argv;
    resv = (struct fpzip_reserved*)finfo->reserved;

    isdouble = (finfo->type == FPZIP_TYPE_DOUBLE);
    prec = finfo->precision;
    crossproduct = resv->crossproduct;

    /* Element size (in bytes) */
    elemsize = (isdouble ? sizeof(double) : sizeof(float));

    /* size of uncompressed data */
    databytes = crossproduct * elemsize;

    if(flags & H5Z_FLAG_REVERSE) {
        /** Decompress data **/

	/* Tell fpzip where to get the compressed data */
        fpz = fpzip_read_from_buffer(*buf);
        if(fpzip_errno != fpzipSuccess)
	    goto cleanupAndFail;

	/* Fill fpz */
	fpz->type = finfo->type;
	fpz->prec = finfo->precision;
	fpz->nx = resv->nx;
	fpz->ny = resv->ny;
	fpz->nz = resv->nz;
	fpz->nf = resv->nf;

        /* Create the decompressed data buffer */
	outbuf = (char*)malloc(databytes);

        /* Decompress into the output data buffer */
        outbuf_used = fpzip_read(fpz,outbuf);

        if(fpzip_errno == fpzipSuccess && outbuf_used == 0)
            fpzip_errno = fpzipErrorReadStream;

        if(fpzip_errno != fpzipSuccess)
	    goto cleanupAndFail;

        fpzip_read_close(fpz);
        if(fpzip_errno != fpzipSuccess)
	    goto cleanupAndFail;

        /* Replace the buffer given to us with our decompressed data buffer */
        free(*buf);
        *buf = outbuf;
        *buf_size = databytes;
        outbuf = NULL;
        return outbuf_used; /* # valid bytes */

    } else {
  
        /** Compress data **/

        /* Create the compressed data buffer */
        /* This is overkill because compression is smaller than uncompressed */
        outbuf = (char*)malloc(databytes);

	fpz = fpzip_write_to_buffer(outbuf,databytes);
	if(fpzip_errno != fpzipSuccess)
  	    goto cleanupAndFail;

	/* Fill fpz */
	fpz->type = finfo->type;
	fpz->prec = finfo->precision;
	fpz->nx = resv->nx;
	fpz->ny = resv->ny;
	fpz->nz = resv->nz;
	fpz->nf = resv->nf;

	outbuf_used = fpzip_write(fpz,*buf);
	if(outbuf_used == 0 && fpzip_errno  == fpzipSuccess)
	    fpzip_errno = fpzipErrorWriteStream;
	if(fpzip_errno != fpzipSuccess)
	    goto cleanupAndFail;
	fpzip_write_close(fpz);
	if(fpzip_errno != fpzipSuccess)
	    goto cleanupAndFail;
	
	/* Replace the buffer given to us with our decompressed data buffer */
	free(*buf);
	*buf = outbuf;
	*buf_size = databytes;
        outbuf = NULL;
        return outbuf_used; /* # valid bytes */
    }

cleanupAndFail:
    if(outbuf)
        free(outbuf);
    if(fpzip_errno != fpzipSuccess) {
	fprintf(stderr,"fpzip error: %s\n",fpzip_errstr[fpzip_errno]);
        fflush(stderr);
    }
    return 0;
}
#endif    

/**************************************************/

static int
zfp_valid(nc_compression_t* info, int rank, size_t* chunksizes)
{
    int stat = NC_NOERR;
    if(rank < 0 || rank > NC_COMPRESSION_MAX_DIMS) {
	LOG((1,"zfp: rank too large: %d",rank));
	return NC_EINVAL;
    }
  {
    struct zfp_params* zfp = &info->zfp;
    if(zfp->type != zfp_type_double && zfp->type != zfp_type_float) {
	LOG((1,"zfp: only double and float types currently supported"));
	return NC_EINVAL;
    }
#if 0
Add tests for:
zfp->minbits
zfp->maxbits
zfp->maxprec
zfp->minexp
#endif
  }
    return THROW(stat);
}

static int
zfp_init(size_t argc, unsigned int* argv, int rank, size_t* chunksizes)
{
    int stat = NC_NOERR;
    int choice,gtone,i,pseudorank;
    size_t crossproduct,elemsize;
    size_t nx,ny,nz,nzsize;
    zfp_field* field = NULL;
    zfp_type type;
    nc_compression_t* info = (nc_compression_t*)argv;
    struct zfp_params* zinfo = NULL;
    struct zfp_reserved* zres = NULL;
    
    if(argc < NC_NELEMS_ZFP) {
	LOG((1,"zfp: argc mismatch: %d::%d",argc,NC_NELEMS_ZFP));
	return NC_ECOMPRESS;
    }

    /* Push off validation */
    stat = zfp_valid(info,rank,chunksizes);
    if(stat != NC_NOERR)
	goto done;

    zinfo = &info->zfp;
    zres = (struct zfp_reserved*)zinfo->reserved;

    type = (zfp_type)zinfo->type;

    /* Compute total size of the chunk and if # of chunks with size > 1 */
    for(gtone=0,crossproduct=1,i=0;i<rank;i++) {
	crossproduct *= chunksizes[i];
	if(chunksizes[i] > 1) gtone++;
    }

    /* ZFP cannot handle more than 3 dims */
    pseudorank = (rank >= 3 ? 3 : rank);

#ifdef CHOICE
    /* If we have exactly 3 dims of size 1, and more than 3 dimensions,
       then merge down to 3 dimensions
    */
    choice = (gtone == 3 && rank > 3);
#else
    choice = 0;
#endif
    if(choice) {
        nx = ny = nz = 1;
        for(i=0;i<rank;i++) {
            if(chunksizes[i] > 1) {
                if(nx == 1) nx = chunksizes[i];
                else if(ny == 1) ny = chunksizes[i];
                else if(nz == 1) nz = chunksizes[i];
            }
        }
    } else { /*prefix*/
        /* Do some computations */
        nzsize = 0;
        if(rank > 2) {
            for(nzsize=1,i=2;i<rank;i++) {
                nzsize *= chunksizes[i];
            }
        }
    }

    if(!choice) {/*prefix*/
	nx = chunksizes[0];
	ny = (rank >= 2 ? chunksizes[1] : 0);
	nz = (rank >= 3 ? nzsize : 0);
    }
    zres->nx = nx;
    zres->ny = ny;
    zres->nz = nz;
    
    /* Set additional info */
    if(zinfo->minbits == 0) zinfo->minbits = ZFP_MIN_BITS;
    if(zinfo->maxbits == 0) zinfo->maxbits = ZFP_MAX_BITS;
    if(zinfo->maxprec == 0) zinfo->maxprec = ZFP_MAX_PREC;
    if(zinfo->minexp == 0) zinfo->minexp = ZFP_MIN_EXP;

#if 0
    /* Setup the concise parameters */
    zstream = zfp_stream_open(NULL);
    if(zstream == NULL)	{stat = NC_ENOMEM; goto done;}
    zfp_stream_set_accuracy(zstream, zinfo->tolerance, type);
    zfp_stream_set_precision(zstream, zinfo->precision, type);
    zfp_stream_set_rate(zstream, zinfo->rate, type, rank, 0);
    zres->zfp_params = zfp_stream_mode(zstream);
    zfp_stream_close(zstream);
#endif
    zres->crossproduct = crossproduct;
    zres->rank = pseudorank;

#ifdef DEBUG
    fprintf(stderr,"zfp: final params: minbits=%u maxbits=%u maxprec=%u minexp=%d\n",
	    zinfo->minbits,zinfo->maxbits,zinfo->maxprec,zinfo->minexp);
    fflush(stderr);
#endif

done:
    return THROW(stat);
}

/**
Assumptions:
1. Each incoming block represents one complete chunks
*/
static size_t
H5Z_filter_zfp(unsigned int flags, size_t cd_nelmts,
                     const unsigned int argv[], size_t nbytes,
                     size_t *buf_size, void **buf)
{
    int stat = NC_NOERR;
    zfp_field* zfp;
    char *outbuf = NULL;
    size_t databytes;
    size_t elemsize;
    size_t crossproduct;
    size_t outbuf_used = 0;
    zfp_stream* zstream = NULL;
    bitstream* bstream = NULL;
    zfp_field* field = NULL;
    int isdouble,nchunks;
    zfp_type type;
    struct zfp_params* zinfo = NULL;
    struct zfp_reserved* zres = NULL;
    
    if(nbytes == 0) return 0; /* sanity check */

    zinfo = (struct zfp_params*)argv;
    zres = (struct zfp_reserved*)zinfo->reserved;

    type = (zfp_type)zinfo->type;
    isdouble = (type == zfp_type_double);
    crossproduct = zres->crossproduct;

    /* Element size (in bytes) */
    elemsize = (isdouble ? sizeof(double) : sizeof(float));

    /* size of uncompressed data */
    databytes = crossproduct * elemsize;

    /* Build a field */
    switch (zres->rank) {
    case 1: field = zfp_field_1d(NULL,type,zres->nx); break;
    case 2: field = zfp_field_2d(NULL,type,zres->nx,zres->ny); break;
    default: field = zfp_field_3d(NULL,type,zres->nx,zres->ny,zres->nz); break;
    }
    if(field == NULL)
	{stat = THROW(NC_ENOMEM); goto done;}

    /* always use stride 1 */
    field->sx = field->sy = field->sz = 0; 

    /* Build zfp stream*/
    zstream = zfp_stream_open(NULL);
    if(zstream == NULL)
	{stat = THROW(NC_ENOMEM); goto done;}

    /* Set stream parameters */    
    if(!zfp_stream_set_params(zstream,
				zinfo->minbits,
				zinfo->maxbits,
				zinfo->maxprec,
				zinfo->minexp))
	{stat = THROW(NC_ECOMPRESS); goto done;}

    if(flags & H5Z_FLAG_REVERSE) {
        /** Decompress data **/

        /* Build a bit stream */
        bstream = stream_open(*buf,*buf_size);
        if(bstream == NULL)
	    {stat = THROW(NC_ENOMEM); goto done;}
	zfp_stream_set_bit_stream(zstream,bstream);

        /* Create the decompressed data buffer and tell zfp */
        outbuf = (char*)malloc(databytes);
        zfp_field_set_pointer(field, outbuf);
	outbuf_used = databytes; /* assume all is used */

        /* Decompress into the stream */
	zfp_stream_rewind(zstream);
	if(zfp_decompress(zstream,field) == 0)
	    {stat = THROW(NC_ECOMPRESS); goto done;}

        /* Replace the buffer given to us with our decompressed data buffer */
        free(*buf);
        *buf = outbuf;
        *buf_size = databytes;
	outbuf = NULL;
        return outbuf_used; /* # valid bytes */

    } else {
  
        /** Compress data **/

	/* Tell zfp about decompressed data */
	zfp_field_set_pointer(field, *buf);

        /* Create the compressed data buffer and tell zfp */

	/* Recompute output size */
	databytes = zfp_stream_maximum_size(zstream, field);
        if(!databytes)
	    {stat = THROW(NC_ECOMPRESS); goto done;}
        outbuf = (char*)malloc(databytes);
	if(outbuf == NULL)
	    {stat = THROW(NC_ENOMEM); goto done;}

        /* Build a bit stream */
        bstream = stream_open(outbuf,databytes);
        if(bstream == NULL)
	    {stat = THROW(NC_ENOMEM); goto done;}
	zfp_stream_set_bit_stream(zstream,bstream);

        /* Decompress into the stream */
	zfp_stream_rewind(zstream);
	outbuf_used = zfp_compress(zstream,field);
	if(outbuf_used == 0)
	    {stat = THROW(NC_ECOMPRESS); goto done;}

        /* Replace the buffer given to us with our compressed data buffer */
        free(*buf);
        *buf = outbuf;
        *buf_size = databytes;
	outbuf = NULL;
        return outbuf_used; /* # valid bytes */
    }

done:
    if(field != NULL) zfp_field_free(field);
    if(zstream != NULL) zfp_stream_close(zstream);
    if(bstream != NULL) stream_close(bstream);
    return THROW(stat);
}

/**************************************************/
/* Utilities */

const char*
NC_algorithm_name(H5Z_filter_t id)
{
    NCC_COMPRESSOR* p;
    for(p=compressors;p->filterid != H5Z_FILTER_NOZIP;p++) {
	if(p->filterid == id) return p->name;
    }
    return NULL;    
}

H5Z_filter_t
NC_algorithm_id(const char* name)
{
    NCC_COMPRESSOR* p;
    for(p=compressors;p->filterid != H5Z_FILTER_NOZIP;p++) {
	if(strcmp(name,p->name)==0) return p->filterid;
    }
    return H5Z_FILTER_NOZIP;    
}

/* get compressor info by enum */
static NCC_COMPRESSOR*
NC_compressor_for(H5Z_filter_t index)
{
    NCC_COMPRESSOR* p;
    for(p=compressors;p->filterid != H5Z_FILTER_NOZIP;p++) {
	if(p->filterid == index) return p;
    }
    return NULL;
}

H5Z_filter_t
NC_algorithm_for_filter(H5Z_filter_t filterid)
{
    NCC_COMPRESSOR* p;
    for(p=compressors;p->filterid != H5Z_FILTER_NOZIP;p++) {
	if(p->filterid == filterid) return p->filterid;
    }
    return H5Z_FILTER_NOZIP;
}

#if 0
/* Convert NC_compression_info -> NC_compression_t */
int
NC_compress_cvt_from(NC_compression_info* src, size_t dstsize, unsigned int* dst0)
{
    int stat = NC_NOERR;
    nc_compression_t* dst = (nc_compression_t*)dst0;
    switch (src->algorithm) {
    case NC_ZIP:
	if(NC_NELEMS_ZIP > dstsize) return NC_ECOMPRESS;
        dst->zip = src->params.zip;
	break;
    case NC_BZIP2:
	if(NC_NELEMS_BZIP2 > dstsize) return NC_ECOMPRESS;
        dst->bzip2 = src->params.bzip2;
	break;
    case NC_SZIP:
	if(NC_NELEMS_SZIP > dstsize) return NC_ECOMPRESS;
        dst->szip = src->params.szip;
	break;
    case NC_FPZIP:
	if(NC_NELEMS_FPZIP > dstsize) return NC_ECOMPRESS;
	dst->fpzip = src->params.fpzip;
	break;
    case NC_ZFP:
	if(NC_NELEMS_ZFP > dstsize) return NC_ECOMPRESS;
	dst->zfp = src->params.zfp;
	break;
    default:
	stat = NC_ECOMPRESS;
    }
    return THROW(stat);
}

/* Convert nc_compression_t -> NC_compression_info */
int
NC_compress_cvt_to(H5Z_filter_t alg, size_t srcsize, unsigned int* src0, NC_compression_info* dst)
{
    int stat = NC_NOERR;
    nc_compression_t* src = (nc_compression_t*)src0;

    dst->algorithm = alg;
    switch (alg) {
    case NC_ZIP:
        if(sizeof(src->zip) < srcsize) return NC_ECOMPRESS;
	dst->argc = NC_NELEMS_ZIP;	
        dst->params.zip = src->zip;
	break;
    case NC_BZIP2:
        if(sizeof(src->bzip2) < srcsize) return NC_ECOMPRESS;
	dst->argc = NC_NELEMS_BZIP2;
        dst->params.bzip2 = src->bzip2;
	break;
    case NC_SZIP:
        if(sizeof(src->szip) < srcsize) return NC_ECOMPRESS;
	dst->argc = NC_NELEMS_SZIP;
        dst->params.szip = src->szip;
	break;
    case NC_FPZIP:
        if(sizeof(src->fpzip) < srcsize) return NC_ECOMPRESS;
	dst->argc = NC_NELEMS_FPZIP;
	dst->params.fpzip = src->fpzip;
	break;
    case NC_ZFP:
        if(sizeof(src->zfp) < srcsize) return NC_ECOMPRESS;
	dst->argc = NC_NELEMS_ZFP;
	dst->params.zfp = src->zfp;
	break;
    default:
	stat = NC_ECOMPRESS;
    }
    return THROW(stat);
}
#endif /*0*/

size_t
NC_algorithm_nelems(H5Z_filter_t alg)
{
    NCC_COMPRESSOR* cmp;
    cmp = NC_compressor_for(alg);
    if(cmp == NULL)
	return 0;
    return cmp->nelems;
}


/**************************************************/

size_t
nc_inq_algorithm_argc(const char* algname)
{
    H5Z_filter_t alg = NC_algorithm_id(algname);
    if(alg == H5Z_FILTER_NOZIP) return 0;
    return NC_algorithm_nelems(alg);
}

static const char* algorithm_names[NC_NCOMPRESSORS+1] = {NULL};

/* Get set of known algorithms by name */
const char**
nc_inq_algorithm_names(void)
{
    if(NC_compress_initialized == 0)
	NC_compress_register_all();
    if(algorithm_names[0] == NULL) {
	int i;
	char** names = (char**)algorithm_names; /* break const */
	NCC_COMPRESSOR* cmp = compressors;
	for(i=0;cmp->filterid;cmp++) {
	    if(cmp->registered)
		names[i++] = (char*)cmp->name; /* remove const */
	}
	names[i] = NULL; /* NULL terminate */
    }
    return algorithm_names;
}

/**************************************************/
#ifdef VERIFYSIZE
static void
verifysize()
{
    int i;
    nc_compression_t info;
    for(i=0;i<NC_NCOMPRESSORS;i++) {
	int defined, computed, usize;
	switch (i) {
        case H5Z_FILTER_DEFLATE:
	    computed = sizeof(info.zip);
	    defined = NC_NELEMS_ZIP;
	    break;
        case H5Z_FILTER_SZIP:
	    computed = sizeof(info.szip);
	    defined = NC_NELEMS_SZIP;
	    break;
        case H5Z_FILTER_BZIP2:
	    computed = sizeof(info.bzip2);
	    defined = NC_NELEMS_BZIP2;
	    break;
        case H5Z_FILTER_FPZIP:
	    computed = sizeof(info.fpzip);
	    defined = NC_NELEMS_FPZIP;
	    break;
        case H5Z_FILTER_ZFP:
	    computed = sizeof(info.zfp);
	    defined = NC_NELEMS_ZFP;
	    break;
	default:
	    computed = 0;
	    defined = 0;
	    break; /* ignore */
	}
	usize = computed / sizeof(unsigned int); /*fixup*/
        if((usize * sizeof(unsigned int)) != computed)
	    fprintf(stderr,"%s: partial size; computed=%d usize=%d\n",
			NC_algorithm_name(i),computed,usize);
	if(usize != defined) {
	    fprintf(stderr,"%s: size mismatch; computed=%d defined=%d\n",
			NC_algorithm_name(i),usize,defined);
	}
    }
}
#endif

/**************************************************/

/* Provide access to all the compressors */
static NCC_COMPRESSOR compressors[NC_NCOMPRESSORS+1] = {
    {H5Z_FILTER_DEFLATE, "zip", NC_NELEMS_ZIP, 0, NULL, zip_init},
    {H5Z_FILTER_SZIP, "szip", NC_NELEMS_SZIP, 0, NULL, szip_init},
    {H5Z_FILTER_BZIP2, "bzip2", NC_NELEMS_BZIP2, 0, H5Z_filter_bzip2, bzip2_init},
    {H5Z_FILTER_FPZIP, "fpzip", NC_NELEMS_FPZIP, 0, H5Z_filter_fpzip, fpzip_init},
    {H5Z_FILTER_ZFP, "zfp", NC_NELEMS_ZFP, 0, H5Z_filter_zfp, zfp_init},
    {0, "", 0, 0, NULL, NULL} /* must be last */
};
