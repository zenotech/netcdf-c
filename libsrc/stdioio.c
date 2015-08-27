/*
This file was an attempt to convert
posixio to use stdio (e.g. fread).
It is only a partial conversion
since the posixio buffer is still
used, so it is not clear if
it is worth the effort to complete.
*/

/*
 *      Copyright 1996, University Corporation for Atmospheric Research
 *      See netcdf/COPYRIGHT file for copying and redistribution conditions.
 */
/* Dennis Heimbigner 2015-8-20 */

#include "config.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>      /* DEBUG */
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>      /* DEBUG */
#endif
#include <assert.h>

#ifndef _WIN32_WCE
#include <errno.h>
#else
#define EPERM  NC_EPERM
#define ENOMEM NC_ENOMEM
#define EINVAL NC_EINVAL
#define EIO    NC_EINVAL
#define EEXIST NC_EEXIST
#endif

#if 0
#ifndef X_ALIGN
#define X_ALIGN 4
#endif
#endif

#ifndef ENOERR
#define ENOERR NC_NOERR
#endif

#include <string.h>
#include <stdio.h>

#include "ncio.h"
#include "fbits.h"
#include "rnd.h"

#if !defined(NDEBUG) && !defined(X_INT_MAX)
#define  X_INT_MAX 2147483647
#endif
#if 0 /* !defined(NDEBUG) && !defined(X_ALIGN) */
#define  X_ALIGN 4
#endif

#define ALWAYS_NC_SHARE 0 /* DEBUG */

#define DEFAULTBLKSIZE 32768

/**************************************************/
/* Forward */

static int stdio_fileio_rel(ncio* const, off_t, int);
static int stdio_fileio_get(ncio* const, off_t, size_t, int, void **const);
static int stdio_fileio_move(ncio* const, off_t, off_t, size_t, int);
static int stdio_fileio_sync(ncio* const nciop);
static void stdio_fileio_free(void *const pvt);
static int stdio_fileio_init2(ncio* const nciop, size_t* sizehintp);
static int stdio_fileio_init(ncio* const nciop);
static void stdio_free(ncio* nciop);
static ncio* stdio_new(const char* path, int ioflags);
static int stdio_filesize(ncio* nciop, off_t *filesizep);
static int stdio_pad_length(ncio* nciop, off_t length);
static int stdio_close(ncio* nciop, int doUnlink);

/**************************************************/
/* ncio.h private data */

/* Borrow the buffer structure from posixio.c
   blksz - block size for reads and writes to file.
   pos - current read/write position in file.
   bf_offset - file offset corresponding to start of memory buffer
   bf_extent - number of bytes in I/O request
   bf_cnt - number of bytes available in buffer
   bf_base - pointer to beginning of buffer.
   bf_rflags - buffer region flags (defined in ncio.h) tell the lock
   status, read/write permissions, and modification status of regions
   of data in the buffer.
   bf_refcount - buffer reference count.
   slave - used in moves.
*/
typedef struct stdio_ffio {
	size_t blksz;
	off_t pos;
	/* buffer */
	off_t	bf_offset;
	size_t	bf_extent;
	size_t	bf_cnt;
	void	*bf_base;
	int	bf_rflags;
	int	bf_refcount;
	/* chain for double buffering in px_move */
	struct s3_buffer *slave;
} stdio_ffio;

typedef struct stdio_pvt {
    FILE* file;
    stdio_ffio* ffio;
} stdio_pvt;

/**************************************************/
/* Begin OS */

/*
 * What is the preferred I/O block size?
 * (This becomes the default *sizehint == ncp->chunk in the higher layers.)
 * TODO: What is the the best answer here?
 */
static size_t
blksize(stdio_pvt* pvt)
{
    return (size_t) DEFAULTBLKSIZE;
}

/*
 * Sortof like ftruncate, except won't make the
 * file shorter.
 */
static int
fgrow(FILE* f, const off_t len)
{
    long pos;
    long size;

    pos = ftell(f);    
    (void) fseek(f,0,SEEK_END);
    if(ferror(f)) return EIO;
    size = ftell(f);
    (void) fseek(f,pos,SEEK_SET);
    if(ferror(f)) return EIO;
    if(len < size) return ENOERR;
    else {
        const long dumb = 0;
        (void) fseek(f, len-sizeof(dumb), SEEK_SET);
        if(ferror(f)) return EIO;
        (void)fwrite((const void *)&dumb, 1, sizeof(dumb), f);
        if(ferror(f)) return EIO;
        (void) fseek(f, pos, SEEK_SET);
        if(ferror(f)) return EIO;
    }
    return ENOERR;
}

/*
 * Sortof like ftruncate, except won't make the file shorter.  Differs
 * from fgrow by only writing one byte at designated seek position, if
 * needed.
 */
static int
fgrow2(FILE* f, const off_t len)
{
    long pos;
    long size;

    pos = ftell(f);    
    (void) fseek(f,0,SEEK_END);
    if(ferror(f)) return EIO;
    size = ftell(f);
    (void) fseek(f,pos,SEEK_SET);
    if(ferror(f)) return EIO;
    if(len < size) return ENOERR;
    else {
        const char dumb = 0;
        (void) fseek(f, len-sizeof(dumb), SEEK_SET);
        if(ferror(f)) return EIO;
        (void) fwrite((const void *)&dumb, 1, sizeof(dumb), f);
        if(ferror(f)) return EIO;
        (void) fseek(f, pos, SEEK_SET);
        if(ferror(f)) return EIO;
    }
    return ENOERR;
}
/* End OS */

/**************************************************/
/* Begin ffio */

static int
fileio_pgout(ncio* const nciop, 
	off_t const offset,  const size_t extent,
	const void* const vp, off_t* posp)
{
    stdio_pvt* pvt = (stdio_pvt*)nciop->pvt;
    FILE* f = pvt->file;

#ifdef X_ALIGN
    assert((offset % X_ALIGN) == 0);
    assert((extent % X_ALIGN) == 0);
#endif
    if(*posp != offset) {
	(void) fseek(f, offset, SEEK_SET);
	if(ferror(f)) return EIO;
	*posp = offset;
    }
    (void)fwrite(vp,1,extent,f);
    if(ferror(f)) return EIO;
    *posp += extent;
    return ENOERR;
}

static int
fileio_pgin(ncio* const nciop,
	off_t const offset, const size_t extent,
	void *const vp, size_t *nreadp, off_t *posp)
{
    stdio_pvt* pvt = (stdio_pvt*)nciop->pvt;
    FILE* f = pvt->file;
    ssize_t nread;

#ifdef X_ALIGN
    assert(offset % X_ALIGN == 0);
    assert(extent % X_ALIGN == 0);
#endif
    if(*posp != offset) {
	(void) fseek(f, offset, SEEK_SET);
	if(ferror(f)) return EIO;
	*posp = offset;
    }
    nread = fread(vp,1,extent,f);
    if(ferror(f)) return EIO;
    *nreadp = nread;
    *posp += nread;
    return ENOERR;
}

/**************************************************/

static int
stdio_fileio_rel(ncio* const nciop, off_t offset, int rflags)
{
    int status = ENOERR;
    stdio_pvt* pvt = (stdio_pvt*)nciop->pvt;
    stdio_ffio* ffp = pvt->ffio;

    assert(ffp->bf_offset <= offset);
    assert(ffp->bf_cnt != 0);
    assert(ffp->bf_cnt <= ffp->bf_extent);
#ifdef X_ALIGN
    assert(offset <= ffp->bf_offset + X_ALIGN);
    assert(ffp->bf_cnt % X_ALIGN == 0 );
#endif
    if(fIsSet(rflags, RGN_MODIFIED)) {
        if(!fIsSet(nciop->ioflags, NC_WRITE))
            return EPERM; /* attempt to write readonly file */
	status = fileio_pgout(nciop, ffp->bf_offset,
			      ffp->bf_cnt, ffp->bf_base, &ffp->pos);
        /* if error, invalidate buffer anyway */
    }
    ffp->bf_offset = OFF_NONE;
    ffp->bf_cnt = 0;
    return status;
}

static int
stdio_fileio_get(ncio* const nciop,
            off_t offset, size_t extent,
            int rflags,
            void **const vpp)
{
    int status = ENOERR;
    stdio_pvt* pvt = (stdio_pvt*)nciop->pvt;
    stdio_ffio* ffp = pvt->ffio;
#ifdef X_ALIGN
    size_t rndup;
    size_t rem;
#endif
    if(fIsSet(rflags, RGN_WRITE) && !fIsSet(nciop->ioflags, NC_WRITE))
        return EPERM; /* attempt to write readonly file */

    assert(extent != 0);
    assert(extent < X_INT_MAX); /* sanity check */
    assert(ffp->bf_cnt == 0);

#ifdef X_ALIGN
    /* round to seekable boundaries */
    rem = offset % X_ALIGN;
    if(rem != 0) {
	offset -= rem;
	extent += rem;
    }
    rndup = extent % X_ALIGN;
    if(rndup != 0)
	extent += X_ALIGN - rndup;

    assert(offset % X_ALIGN == 0);
    assert(extent % X_ALIGN == 0);
#endif

    if(ffp->bf_extent < extent) {
	if(ffp->bf_base != NULL) {
	    free(ffp->bf_base);
            ffp->bf_base = NULL;
            ffp->bf_extent = 0;
        }
        assert(ffp->bf_extent == 0);
        ffp->bf_base = malloc(extent);
        if(ffp->bf_base == NULL)
            return ENOMEM;
        ffp->bf_extent = extent;
    }
    status = fileio_pgin(nciop, offset, extent,
                         ffp->bf_base, &ffp->bf_cnt, &ffp->pos);
    if(status != ENOERR)
	return status;
    ffp->bf_offset = offset;
    if(ffp->bf_cnt < extent) {
	(void) memset((char *)ffp->bf_base + ffp->bf_cnt, 0,
			extent - ffp->bf_cnt);
        ffp->bf_cnt = extent;
    }

#ifdef X_ALIGN
    *vpp = (char *)ffp->bf_base + rem;
#else
    *vpp = (char *)ffp->bf_base;
#endif
    return ENOERR;
}

static int
stdio_fileio_move(ncio* const nciop, off_t to, off_t from,
                    size_t nbytes, int rflags)
{
    int status = ENOERR;
    off_t lower = from;     
    off_t upper = to;
    char *base;
    size_t diff = upper - lower;
    size_t extent = diff + nbytes;

    rflags &= RGN_NOLOCK; /* filter unwanted flags */

    if(to == from)
        return ENOERR; /* NOOP */
    if(to > from) { /* growing */
	lower = from;   
	upper = to;
    } else { /* shrinking */
	lower = to;
	upper = from;
    }
    diff = upper - lower;
    extent = diff + nbytes;
    status = stdio_fileio_get(nciop, lower, extent, RGN_WRITE|rflags,
                    (void **)&base);
    if(status != ENOERR)
        return status;
    if(to > from)
        (void) memmove(base + diff, base, nbytes); 
    else
        (void) memmove(base, base + diff, nbytes); 
    (void) stdio_fileio_rel(nciop, lower, RGN_MODIFIED);
    return status;
}

static int
stdio_fileio_sync(ncio* const nciop)
{
    stdio_pvt* pvt = (stdio_pvt*)nciop->pvt;
    fflush(pvt->file);
    return ENOERR;
}

static void
stdio_fileio_free(void *const pvt)
{
    stdio_pvt* pv = (stdio_pvt*)pvt;
    stdio_ffio* ffp = (stdio_ffio*)pv->ffio;
    if(ffp == NULL)
	return;
    if(ffp->bf_base != NULL) {
	free(ffp->bf_base);
	ffp->bf_base = NULL;
	ffp->bf_offset = OFF_NONE;
        ffp->bf_extent = 0;
        ffp->bf_cnt = 0;
    }
}

static int
stdio_fileio_init2(ncio* const nciop, size_t* sizehintp)
{
    stdio_pvt* pvt = (stdio_pvt*)nciop->pvt;
    stdio_ffio* ffp = (stdio_ffio*)pvt->ffio;
    FILE* f = pvt->file;

    assert(f != NULL);
    ffp->bf_extent = *sizehintp;
    assert(ffp->bf_base == NULL);
    /* this is separate allocation because it may grow */
    ffp->bf_base = malloc(ffp->bf_extent);
    if(ffp->bf_base == NULL) {
	ffp->bf_extent = 0;
	return ENOMEM;
    }
    /* else */
    return ENOERR;
}


static int
stdio_fileio_init(ncio* const nciop)
{
    stdio_pvt* pvt = (stdio_pvt*)nciop->pvt;
    stdio_ffio* ffp = NULL;

    *((ncio_relfunc **)&nciop->rel) = stdio_fileio_rel;
    *((ncio_getfunc **)&nciop->get) = stdio_fileio_get;
    *((ncio_movefunc **)&nciop->move) = stdio_fileio_move;
    *((ncio_syncfunc **)&nciop->sync) = stdio_fileio_sync;
    *((ncio_filesizefunc **)&nciop->filesize) = stdio_filesize;
    *((ncio_pad_lengthfunc **)&nciop->pad_length) = stdio_pad_length;
    *((ncio_closefunc **)&nciop->close) = stdio_close;

    ffp = calloc(1,sizeof(stdio_ffio));
    if(ffp == NULL) return NC_ENOMEM;
    pvt->ffio = ffp;
    ffp->pos = -1;
    ffp->bf_offset = OFF_NONE;
    ffp->bf_extent = 0;
    ffp->bf_cnt = 0;
    ffp->bf_base = NULL;
    return NC_NOERR;
}

/* */

static void
stdio_free(ncio* nciop)
{
    if(nciop == NULL)
	return;
    if(((stdio_pvt*)nciop->pvt) != NULL)
	stdio_fileio_free(nciop->pvt);
    free(nciop);
}

static ncio *
stdio_new(const char* path, int ioflags)
{
    ncio *nciop;

#if ALWAYS_NC_SHARE /* DEBUG */
    fSet(ioflags, NC_SHARE);
#endif

    if(fIsSet(ioflags, NC_SHARE))
        fprintf(stderr, "NC_SHARE not implemented for stdio\n");

    nciop = (ncio*) calloc(1,sizeof(ncio));
    if(nciop == NULL) return NULL;
    nciop->pvt = calloc(1,sizeof(stdio_pvt));
    if(nciop->pvt == NULL) {stdio_free(nciop); return NULL;}
    nciop->path = strdup(path);
    if(nciop->path == NULL) {stdio_free(nciop); return NULL;}
    nciop->ioflags = ioflags;
    *((int *)&nciop->fd) = -1; /* cast away const */
    if(stdio_fileio_init(nciop))
	{stdio_free(nciop); return NULL;}
    return nciop;
}

/* Public below this point */

/* TODO: Is this reasonable for this platform? */
static const size_t STDIO_MINBLOCKSIZE = 0x100;
static const size_t STDIO_MAXBLOCKSIZE = 0x100000;

int
stdio_create(const char *path, int ioflags,
    size_t initialsz,
    off_t igeto, size_t igetsz, size_t *sizehintp,
    void* parameters,
    ncio **nciopp, void **const igetvpp)
{
    ncio *nciop;
#ifdef _WIN32_WCE
    char* oflags = "bw+"; /*==?(O_RDWR|O_CREAT|O_TRUNC) && binary*/
#else
    char* oflags = "w+"; /*==?(O_RDWR|O_CREAT|O_TRUNC);*/
#endif
    FILE* f;
    int status = ENOERR;

    if(initialsz < (size_t)igeto + igetsz)
        initialsz = (size_t)igeto + igetsz;
    fSet(ioflags, NC_WRITE);
    if(path == NULL || *path == 0)
        return EINVAL;
    nciop = stdio_new(path, ioflags);
    if(nciop == NULL)
        return ENOMEM;
    if(fIsSet(ioflags, NC_NOCLOBBER)) {
        /* Since we do not have use of the O_EXCL flag, we need to fake it */
#ifdef WINCE
	f = fopen(path,"rb");
#else
        f = fopen(path,"r");
#endif
	if(f != NULL) { /* do not overwrite */
	    (void)fclose(f);
	    return EEXIST;
        }           
    }
    f = fopen(path, oflags);
    if(f == NULL) {
	status = errno;
	goto unwind_new;
    }
    ((stdio_pvt*)nciop->pvt)->file = f;

    if(*sizehintp < STDIO_MINBLOCKSIZE || *sizehintp > STDIO_MAXBLOCKSIZE) {
	/* Use default */
        *sizehintp = blksize((stdio_pvt*)nciop->pvt);
    } else {
	*sizehintp = M_RNDUP(*sizehintp);
    }
    status = stdio_fileio_init2(nciop, sizehintp);
    if(status != ENOERR)
        goto unwind_open;
    if(initialsz != 0) {
	status = fgrow(f, (off_t)initialsz);
        if(status != ENOERR)
            goto unwind_open;
    }
    if(igetsz != 0) {
	status = nciop->get(nciop,
                            igeto, igetsz,
                            RGN_WRITE,
                            igetvpp);
	if(status != ENOERR)
	goto unwind_open;
    }
    *nciopp = nciop;
    return ENOERR;

unwind_open:
    if(((stdio_pvt*)nciop->pvt)->file != NULL)
	(void) fclose(((stdio_pvt*)nciop->pvt)->file);
    /* ?? unlink */
    /*FALLTHRU*/
unwind_new:
    stdio_free(nciop);
    return status;
}

int
stdio_open(const char *path,
	    int ioflags,
	    off_t igeto, size_t igetsz, size_t *sizehintp,
	    void* parameters,
	    ncio **nciopp, void **const igetvpp)
{
    ncio *nciop;
    char* oflags = fIsSet(ioflags, NC_WRITE) ? "r+"
#ifdef WINCE
                                             : "rb";
#else
                                             : "r";
#endif
    FILE* f;
    int status = ENOERR;
    if(path == NULL || *path == 0)
	return EINVAL;
    nciop = stdio_new(path, ioflags);
    if(nciop == NULL)
	return ENOMEM;
    f = fopen(path, oflags);
    if(f == NULL) {
	status = errno;
	goto unwind_new;
    }
    ((stdio_pvt*)nciop->pvt)->file = f;

    if(*sizehintp < STDIO_MINBLOCKSIZE || *sizehintp > STDIO_MAXBLOCKSIZE)
        *sizehintp = blksize((stdio_pvt*)nciop->pvt);
    else
	*sizehintp = M_RNDUP(*sizehintp);

    status = stdio_fileio_init2(nciop, sizehintp);
    if(status != ENOERR)
        goto unwind_open;

    if(igetsz != 0) {
	status = nciop->get(nciop,
                            igeto, igetsz,
                            0,
                            igetvpp);
	if(status != ENOERR)
            goto unwind_open;
    }
    *nciopp = nciop;
    return ENOERR;

unwind_open:
    if(((stdio_pvt*)nciop->pvt)->file != NULL)
	((stdio_pvt*)nciop->pvt)->file = NULL;
    /*FALLTHRU*/
unwind_new:
    stdio_free(nciop);
    return status;
}

/* 
 * Get file size in bytes.  
 * Is use of fstatus = fseek() really necessary,
 * or could we use standard fstat() call
 * and get st_size member?
 */
static int
stdio_filesize(ncio* nciop, off_t *filesizep)
{
    int status = ENOERR;
    off_t current;
    FILE* f;

    if(nciop == NULL)
        return EINVAL;
    f = ((stdio_pvt*)nciop->pvt)->file;
    current = ftell(f);
    status = fseek(f, 0, SEEK_END); /* get size */
    if(status != ENOERR) return status;
    if(ferror(f)) return EIO;
    *filesizep = ftell(f);
    status = fseek(f, current, SEEK_SET); /* reset */ 
    if(ferror(f)) return EIO;
    return ENOERR;
}

/*
 * Sync any changes to disk, then extend file so its size is length.
 * This is only intended to be called before close, if the file is
 * open for writing and the actual size does not match the calculated
 * size, perhaps as the result of having been previously written in
 * NOFILL mode.
 */
static int
stdio_pad_length(ncio* nciop, off_t length)
{
    int status = ENOERR;
    FILE* f;

    if(nciop == NULL)
	return EINVAL;
    f = ((stdio_pvt*)nciop->pvt)->file;
    if(!fIsSet(nciop->ioflags, NC_WRITE))
        return EPERM; /* attempt to write readonly file */
    status = nciop->sync(nciop);
    if(status != ENOERR)
	return status;
    status = fgrow2(f, length);
    if(status != ENOERR)
	return errno;
    return ENOERR;
}

static int 
stdio_close(ncio* nciop, int doUnlink)
{
    int status = ENOERR;
    FILE* f;

    if(nciop == NULL)
	return EINVAL;
    f = ((stdio_pvt*)nciop->pvt)->file;
    nciop->sync(nciop);
    if(f != NULL) (void) fclose(f);
    ((stdio_pvt*)nciop->pvt)->file = NULL;
    if(doUnlink)
	(void) unlink(nciop->path);
    stdio_free(nciop);
    return status;
}
