/*
  Copyright 2008, UCAR/Unidata
  See COPYRIGHT file for copying and redistribution conditions.

  This program tests the large file bug in netCDF 3.6.2,
  creating byte and short variables larger than 4 GiB.
*/

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "netcdf.h"
#include "netcdf_compress.h"

/* Write using var1_T instead of var_T */
#undef VAR1

#ifdef SZIP_FILTER
#include <szlib.h>
#else
#define SZ_NN_OPTION_MASK 0
#define SZ_EC_OPTION_MASK 0
#endif

#ifdef FPZIP_FILTER
#include <fpzip.h>
#endif

#ifdef ZFP_FILTER
#include <zfp.h>
#endif

#include "netcdf.h"
#include "nc_tests.h"
#include "nc4compress.h"

#define DEFAULTZIPLEVEL 9
#define DEFAULTBZIP2LEVEL 9

#if 1
#define SZIP_MASK SZ_NN_OPTION_MASK;
#else
#define SZIP_MASK SZ_EC_OPTION_MASK;
#endif
#define DEFAULTPPB 32

#define DEFAULTPRECISION 32

#define MAXERRS 8

#define FPZIPCHOOSE 3
#define ZFPCHOOSE 3

/* Following 3 must be consistent */
#define T float
#define NC_PUT_VAR1 nc_put_var1_float
#define NC_PUT_VAR nc_put_var_float

/* Created Meta-data 
netcdf zip {
dimensions:
	dim1 = .. ;
	dim2 = ... ;
	dim3 = ... ;
	...
	dimn = ... ;
variables:
	int var(dim1, dim2, dim3,...dimn) ;
}
*/

typedef enum XZIP {
NOZIP = 0,
ZIP   = 1,
SZIP  = 2,
BZIP2 = 3,
FPZIP = 4,
ZFP   = 5,
} XZIP;
#define NZIP (ZFP+1)

/* These need to be parallel with XZIP */
static const char* zipnames[NZIP+1] = {"nozip","zip","szip","bzip2","fpzip","zfp",NULL};

#define MAXDIMS 8

#define DEFAULTACTUALDIMS 4
#define DEFAULTDIMSIZE 4
#define DEFAULTCHUNKSIZE 4

/* From command line */	
static XZIP test;
static unsigned int params[NC_COMPRESSION_MAX_PARAMS-1];
static int precision = DEFAULTPRECISION;
static unsigned int ziplevel = DEFAULTZIPLEVEL;
static unsigned int bzip2level = DEFAULTBZIP2LEVEL;
static unsigned int pixels_per_block = DEFAULTPPB;

static size_t dimsize = DEFAULTDIMSIZE;
static size_t chunksize = DEFAULTCHUNKSIZE;
static size_t actualdims = DEFAULTACTUALDIMS;
static size_t pattern[MAXDIMS];

static size_t totalproduct = 1; /* x-product over max dims */
static size_t actualproduct = 1; /* x-product over actualdims */
static size_t chunkproduct = 1; /* x-product over actual chunks */
static size_t fpzipproduct = 1; /* x-product over chunks wrt FPZIP_CHOOSE*/
static size_t zfpproduct = 1; /* x-product over chunks wrt ZFP_CHOOSE*/

static size_t dims[MAXDIMS];
static size_t chunks[MAXDIMS];

static int ncid, varid;
static int dimids[MAXDIMS];
static size_t odom[MAXDIMS];
static T* array = NULL;
static T* expected = NULL;

/* Forward */
static int test_zfp(void);
static int test_fpzip(void);
static int test_bzip2(void);
static int test_szip(void);
static int test_zip(void);
static int test_nozip(void);
static void init(int argc, char** argv);
static void reset(void);
static void odom_reset(void);
static int odom_more(void);
static int odom_next(void);
static int odom_offset(void);
static T expectedvalue(void);

#define ERRR do { \
fflush(stdout); /* Make sure our stdout is synced with stderr. */ \
fprintf(stderr, "Sorry! Unexpected result, %s, line: %d\n", \
	__FILE__, __LINE__);				    \
err++; \
} while (0)

static int
check(int err,int line)
{
    if(err != NC_NOERR) {
	fprintf(stderr,"fail (%d): %s\n",line,nc_strerror(err));
    }
    return NC_NOERR;
}

#define CHECK(x) check(x,__LINE__)

static XZIP
xzipfor(const char* name)
{
    XZIP i;
    for(i=0;i<NZIP;i++) {
	if(strcmp(name,zipnames[i])==0)
	    return i;
    }
    return NOZIP;
}

static char*
filenamefor(XZIP encoder)
{
    static char testfile[2048];
    const char* name = zipnames[(int)encoder];
    snprintf(testfile,sizeof(testfile),"%s.nc",name);
    return testfile;
}

static int
verifychunks(void)
{
    int i;
    int store = -1;
    size_t chunksizes[MAXDIMS];
    memset(chunksizes,0,sizeof(chunksizes));
    CHECK(nc_inq_var_chunking(ncid, varid, &store, chunksizes));
    if(store != NC_CHUNKED) {
	fprintf(stderr,"bad chunk store\n");
	return 0;
    }
    for(i=0;i<actualdims;i++) {
        if(chunksizes[i] != chunks[i]) {
	    fprintf(stderr,"bad chunk size: %d\n",i);
	    return 0;
	}
    }
    return 1;
}

static int
create(XZIP encoder)
{
    int i;
    char* testfile = filenamefor(encoder);

    /* Create a file with one big variable. */
    CHECK(nc_create(testfile, NC_NETCDF4|NC_CLOBBER, &ncid));
    CHECK(nc_set_fill(ncid, NC_NOFILL, NULL));
    for(i=0;i<actualdims;i++) {
	char dimname[1024];
	snprintf(dimname,sizeof(dimname),"dim%d",i);
        CHECK(nc_def_dim(ncid, dimname, dims[i], &dimids[i]));
    }
    CHECK(nc_def_var(ncid, "var", NC_FLOAT, actualdims, dimids, &varid));
    return NC_NOERR;
}

static int
open(XZIP encoder)
{
    char algorithm[NC_COMPRESSION_MAX_NAME];
    nc_compression_t parms;
    char* testfile = filenamefor(encoder);
    const char* compressor = zipnames[(int)encoder];
    size_t nparams = NC_COMPRESSION_MAX_PARAMS;	

    /* Open the file and check it. */
    CHECK(nc_open(testfile, NC_NOWRITE, &ncid));
    CHECK(nc_inq_varid(ncid, "var", &varid));

    if(encoder != NOZIP) {
        /* Check the compression algorithm */
        CHECK(nc_inq_var_compress(ncid,varid,algorithm,&nparams,parms.argv));
        if(strcmp(algorithm,compressor) != 0) {
	    printf("Compression algorithm mismatch: %s\n",algorithm);
	    return 0;
        } else	
	    printf("Using compression algorithm: %s\n",algorithm);
    }

    /* Verify chunking */
    if(!verifychunks())
	return 0;
    fflush(stderr);
    return 1;
}

static int
setchunking(void)
{
    int i;
    int store;

    store = NC_CHUNKED;
    CHECK(nc_def_var_chunking(ncid,varid,store,chunks));
    if(!verifychunks())
	return NC_EINVAL;
    return NC_NOERR;
}

static void
fill(void)
{
   odom_reset();
   if(1) {
	int i;
if(actualproduct <= 1) abort();
	for(i=0;i<actualproduct;i++)
	    expected[i] = (T)i;
   } else {
       while(odom_more()) {
	    int offset = odom_offset();
	    T expect = expectedvalue();
	    expected[offset] = expect;
	    odom_next();
	}
   }
}

static int
write(void)
{
   int stat = NC_NOERR;
#ifdef VAR1
   odom_reset();
   while(odom_more()) {
	size_t offset = odom_offset();
	CHECK(NC_PUT_VAR1(ncid,varid,odom,&expected[offset]));
	odom_next();
   }
#else
   stat = NC_PUT_VAR(ncid,varid,expected);
#endif
   return stat;
}


static int
compare(void)
{
    int errs = 0;
    printf("data comparison: |array|=%d\n",actualproduct);
    if(1)
    {
	int i;
	for(i=0;i<actualproduct;i++) {
	    if(expected[i] != array[i]) {
                printf("mismatch: array[%d]=%f expected[%d]=%f\n",
                            i,array[i],i,expected[i]);
                errs++;
                if(errs >= MAXERRS)
                    break;
            }
	}
   } else
   {
       odom_reset();
       while(odom_more()) {
            int offset = odom_offset();
            float expect = expectedvalue();
            if(array[offset] != expect) {
                printf("mismatch: array[%d]=%f expected=%f\n",
                            offset,array[offset],expect);
                errs++;
                if(errs >= MAXERRS)
                    break;
            }
            odom_next();
       }
   }

   if(errs == 0)
        printf("no data errors\n");
   return (errs == 0);
}

static void
showparameters(XZIP encoding, nc_compression_t* parms)
{
    int i;
    printf("%s:",zipnames[encoding]);
    switch (encoding) {
    case ZFP:
        printf(" parameters: "
                   " type=%d"
                   " minbits=%u"
                   " maxbits=%u"
                   " maxprec=%u"
                   " minexp=%d",
	parms->zfp.type,
	parms->zfp.minbits,
	parms->zfp.maxbits,
	parms->zfp.maxprec,
	parms->zfp.minexp);
	break;
    case FPZIP:
        printf(" parameters: "
                   " type=%d"
                   " precision=%d",
	parms->fpzip.type,
	parms->fpzip.precision);
	break;
    case BZIP2:
        printf(" parameters: level=%u",
		parms->bzip2.level);
	break;
    case SZIP:
        printf(" parameters: mask=%0x pixels-per-block=%d",
                parms->szip.options_mask,
		parms->szip.pixels_per_block);
	break;
    case ZIP:
        printf(" parameters: level=%u",
		parms->zip.level);
	break;
    case NOZIP:
        printf(" parameters: rank=%d",actualdims);
	break;		
    default: break;
    }

    for(i=0;i<actualdims;i++)
	printf("%s%d",(i==0?" chunks=":","),chunks[i]);
    printf("\n");
}

static int
test_zfp(void)
{
    int ok = 1;
    nc_compression_t parms;

    printf("\n*** Testing zfp compression.\n");

    /* Use zfp compression */
    memcpy(&parms.argv[1],params,sizeof(params));

    parms.zfp.type      = zfp_type_float; /* single or double precision */

    create(ZFP);
    setchunking();
    showparameters(ZFP,&parms);

    CHECK(nc_def_var_compress(ncid, varid, "zfp", NC_NELEMS_ZFP,parms.argv));
    CHECK(nc_enddef(ncid));

    /* Fill in the array */
    fill();
    /* write array */
    CHECK(write());
    CHECK(nc_close(ncid));

    printf("\n*** Testing zfp decompression.\n");
    reset();
    open(ZFP);
    CHECK(nc_get_var_float(ncid, varid, array));
    ok = compare();
    CHECK(nc_close(ncid));
    return ok;
}

static int
test_fpzip(void)
{
    int ok = 1;
    nc_compression_t parms;

    printf("\n*** Testing fpzip compression.\n");
    reset();

    /* Use fpzip compression */
    parms.fpzip.type = 0; /* single (0) or double (1) precision */
    parms.fpzip.precision  = 0; /* number of bits of precision (zero = full) */

    create(FPZIP);
    setchunking();
    showparameters(FPZIP,&parms);

    CHECK(nc_def_var_compress(ncid, varid, "fpzip", NC_NELEMS_FPZIP, parms.argv));
    CHECK(nc_enddef(ncid));

    /* Fill in the array */
    fill();
    /* write array */
    CHECK(write());
    CHECK(nc_close(ncid));

    printf("\n*** Testing fpzip decompression.\n");
    reset();
    open(FPZIP);
    CHECK(nc_get_var_float(ncid, varid, array));
    ok = compare();
    CHECK(nc_close(ncid));
    return ok;
}

static int
test_bzip2(void)
{
    int ok = 1;
    nc_compression_t parms;

    printf("\n*** Testing bzip2 compression.\n");
    reset();

    parms.bzip2.level = bzip2level;

    create(BZIP2);
    setchunking();
    showparameters(BZIP2,&parms);

    CHECK(nc_def_var_compress(ncid, varid, "bzip2", NC_NELEMS_BZIP2, parms.argv));
    CHECK(nc_enddef(ncid));

    /* Fill in the array */
    fill();
    /* write array */
    CHECK(write());
    CHECK(nc_close(ncid));

    printf("\n*** Testing bzip2 decompression.\n");
    reset();
    open(BZIP2);
    CHECK(nc_get_var_float(ncid, varid, array));
    ok = compare();
    CHECK(nc_close(ncid));
    return ok;
}

static int
test_szip(void)
{
    int ok = 1;
    nc_compression_t parms;

    printf("\n*** Testing szip compression.\n");
    reset();

    /* Use szip compression */
    parms.szip.options_mask = SZIP_MASK;
    parms.szip.pixels_per_block = 32;

    create(SZIP);
    setchunking();
    showparameters(SZIP,&parms);

    CHECK(nc_def_var_compress(ncid, varid, "szip", NC_NELEMS_SZIP, parms.argv));
    CHECK(nc_enddef(ncid));

    /* Fill in the array */
    fill();
    /* write array */
    CHECK(write());
    CHECK(nc_close(ncid));

    printf("\n*** Testing szip decompression.\n");
    reset();
    open(SZIP);
    CHECK(nc_get_var_float(ncid, varid, array));
    ok = compare();
    CHECK(nc_close(ncid));
    return ok;
}

static int
test_zip(void)
{
    int ok = 1;
    nc_compression_t parms;

    printf("\n*** Testing zip compression.\n");
    reset();

    /* Use zip compression */
    parms.zip.level = ziplevel;

    create(ZIP);
    setchunking();
    showparameters(ZIP,&parms);

    CHECK(nc_def_var_compress(ncid, varid, "zip", NC_NELEMS_ZIP, parms.argv));
    CHECK(nc_enddef(ncid));

    /* Fill in the array */
    fill();
    /* write array */
    CHECK(write());
    CHECK(nc_close(ncid));

    printf("\n*** Testing zip decompression.\n");
    reset();
    open(ZIP);
    CHECK(nc_get_var_float(ncid, varid, array));
    ok = compare();
    CHECK(nc_close(ncid));
    return ok;
}

static int
test_nozip(void)
{
    int ok = 1;

    printf("\n*** Testing nozip compression.\n");
    reset();

    create(NOZIP);
    setchunking();
    showparameters(NOZIP,NULL);

    CHECK(nc_enddef(ncid));

    /* Fill in the array */
    fill();
    /* write array */
    CHECK(write());
    CHECK(nc_close(ncid));

    printf("\n*** Testing nozip decompression.\n");
    reset();
    open(NOZIP);
    CHECK(nc_get_var_float(ncid, varid, array));
    ok = compare();
    CHECK(nc_close(ncid));
    return ok;
}

int
main(int argc, char **argv)
{

    init(argc,argv);

    switch (test) {

    case ZFP:
	if(!test_zfp()) ERRR;
	break;

    case FPZIP:
	if(!test_fpzip()) ERRR;
	break;

    case BZIP2:
	if(!test_bzip2()) ERR;
	break;

    case SZIP:
	if(!test_szip()) ERRR;
	break;

    case ZIP:
	if(!test_zip()) ERRR;
	break;

    case NOZIP:
    default:
	if(!test_nozip()) ERRR;
	break;
    }
    SUMMARIZE_ERR;
    FINAL_RESULTS;
}

/**************************************************/
/* Utilities */

static void
reset()
{
    memset(array,0,sizeof(T)*actualproduct);
}

static void
odom_reset(void)
{
    memset(odom,0,sizeof(odom));
}

static int
odom_more(void)
{
    return (odom[0] < dims[0]);
}

static int
odom_next(void)
{
    int i; /* do not make unsigned */
    for(i=actualdims-1;i>=0;i--) {
        odom[i] += 1;
        if(odom[i] < dims[i]) break;
	if(i == 0) return 0; /* leave the 0th entry if it overflows*/
	odom[i] = 0; /* reset this position*/
    }
    return 1;
}

static int
odom_offset(void)
{
    int i;
    int offset = 0;
    for(i=0;i<actualdims;i++) {
	offset *= dims[i];
	offset += odom[i];
    } 
    return offset;
}

static T
expectedvalue(void)
{
    int i;
    T offset = 0;

    for(i=0;i<actualdims;i++) {
	offset *= dims[i];
	offset += odom[i];
    } 
    return offset;
}

static size_t
getint(const char* arg)
{
    char* p;
    long l = strtol(arg,&p,10);
    if(*p == '\0')
	return (size_t)l;
    fprintf(stderr,"expected integer: found %s\n", arg);
    exit(1);
}

static double
getdouble(const char* arg)
{
    char* p;
    double d = strtod(arg,&p);
    if(*p == '\0')
	return d;
    fprintf(stderr,"expected double: found %s\n", arg);
    exit(1);
}

static void
usage()
{
    fprintf(stderr,
"Usage: tst_compress <options>\n"
"where options are:\n"
"    [-a<unsigned int>] -- specify an algorithm parameter; may be repeated\n"
"    [-d<int>] -- specify number of dimensions to use (<= 5)\n"
"    [-p<int>] -- specify precision (0=>full precision) (fpziponly)\n"
"    [-l<int>] -- specify level (zip,bzip2 only)\n"
"    [-b<int>] -- specify pixels-per-block (szip only)\n"
"    [-h] -- print this message\n"
"    [-nozip|-zip|-szip|-bzip2|-fpzip|-zfp] -- specify the algorithm to test; may not be repeated\n"
);
    exit(1);
}

static void
init(int argc, char** argv)
{
    int i,j,paramcount,found;
    unsigned int nelem = 0;
    XZIP xzip;

    memset(params,0,sizeof(params));

    /* pseudo getopt because windows may not have it */
    paramcount = 0;
    test = NOZIP;
    for(i=1;i<argc;i++) {
	const char* arg = argv[i];
	if(arg[0] != '-') {
	    fprintf(stderr,"bad arg: %s\n",arg);
	    exit(1);
	}
	arg++; /* skip leading '-' */
	xzip = xzipfor(arg);
	if(xzip != NOZIP && test != NOZIP) {
	    usage();
	} else if(xzip != NOZIP) {
	    test = xzip;
	} else {
	    int c = arg[1];
	    switch (c) {
	    case 'a':
		params[paramcount] = (unsigned int)getint(&arg[2]);
		paramcount++;
		break;
	    case 'h': usage();  break;
	    case 'd': actualdims = getint(&arg[2]); break;
	    case 'p': precision = getint(&arg[2]); break;
	    case 'l':
		ziplevel = getint(&arg[2]);
		bzip2level = ziplevel;
		break;
	    case 'b': pixels_per_block = getint(&arg[2]); break;
	    default:
	        fprintf(stderr,"unexpected option: %s\n",arg);
		usage();
	    }
	}
    }

    /* Setup various variables */
    totalproduct = 1;
    fpzipproduct = 1;
    zfpproduct = 1;
    actualproduct = 1;
    chunkproduct = 1;
    for(i=0;i<MAXDIMS;i++) {
	dims[i] = dimsize;
	chunks[i] = (pattern[i] == 1 ? 1 : chunksize);
	totalproduct *= dims[i];
	if(i < actualdims) {
	    actualproduct *= dims[i];
	    chunkproduct *= chunks[i];
   	    fpzipproduct *= (i >= FPZIPCHOOSE ? 1 : chunks[i]);
	    zfpproduct *= (i >= ZFPCHOOSE ? 1 : chunks[i]);
	}
    }
    /* Allocate max size */
    array = (T*)calloc(1,sizeof(T)*actualproduct);
    expected = (T*)calloc(1,sizeof(T)*actualproduct);
}
