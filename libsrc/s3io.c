/*
 *      Copyright 2010, University Corporation for Atmospheric Research
 *      See netcdf/COPYRIGHT file for copying and redistribution conditions.
 */

#include "config.h"

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <assert.h>
#include <errno.h>
#include <string.h>
//#include <sys/types.h>
#if defined HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include <curl/curl.h>
#include "libs3.h"

#include "ncio.h"
#include "fbits.h"
#include "rnd.h"

/* #define INSTRUMENT 1 */
#if INSTRUMENT /* debugging */
#undef NDEBUG
#include <stdio.h>
#include "instr.h"
#endif

#if 0
#define  X_ALIGN 4
#endif

#ifndef ENOERR
#define ENOERR 0
#endif

#undef MIN  /* system may define MIN somewhere and complain */
#define MIN(mm,nn) (((mm) < (nn)) ? (mm) : (nn))

#if !defined(NDEBUG) && !defined(X_INT_MAX)
#define  X_INT_MAX 2147483647
#endif

#if 0 /* !defined(NDEBUG) && !defined(X_ALIGN) */
#define  X_ALIGN 4
#else
#undef X_ALIGN
#endif

#ifndef S3IO_DEFAULT_PAGESIZE
#define S3IO_DEFAULT_PAGESIZE 4096
#endif

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
typedef struct s3_buffer {
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
} s3_buffer;

/*
   blksz - block size for reads and writes to file.
   pos - current read/write position in file.
   buf - s3 buffer for the file
   slave - used in moves.
*/
typedef struct ncio_s3 {
    S3* s3;
    s3_buffer buf;
    /* chain for double buffering in s3_move */
    struct ncio_s3 *slave;
} ncio_s3;

/*Forward*/
static int ncio_s3_move(ncio* const nciop, off_t to, off_t from, size_t nbytes, int rflags);
static void ncio_s3_free(ncio* nciop);
static ncio*  ncio_s3_new(const char *path, int ioflags);
static int ncio_s3_filesize(ncio *nciop, off_t *filesizep);
static int ncio_s3_pad_length(ncio *nciop, off_t length);
static int ncio_s3_close(ncio *nciop, int doUnlink);

/**************************************************/
/* Begin OS */

static int
errcvt(S3error e3)
{
    switch (e3) {
    case S3_OK: return NC_NOERR;
    case S3_ENOMEM: return NC_ENOMEM;
    case S3_EEXIST: return NC_EEXIST;
    case S3_EURL: return NC_EURL;
    case S3_ECURL: return NC_ECURL;
    case S3_EHTTP: return NC_EURL;
    case S3_EAUTH: return NC_EAUTH;
    case S3_EMETA: return NC_EINVAL;
    default: break;
    }
    return S3_ES3;
}

/*
 * What is the system pagesize?
 */
static size_t
pagesize(void)
{
    size_t pgsz;
#if defined(_WIN32) || defined(_WIN64)
    SYSTEM_INFO info;
#endif
    /* Hmm, aren't standards great? */
#if defined(_SC_PAGE_SIZE) && !defined(_SC_PAGESIZE)
#define _SC_PAGESIZE _SC_PAGE_SIZE
#endif

  /* For MinGW Builds */
#if defined(_WIN32) || defined(_WIN64)
    GetSystemInfo(&info);
    pgsz = (size_t)info.dwPageSize;
#elif defined(_SC_PAGESIZE)
    pgsz = (size_t)sysconf(_SC_PAGESIZE);
#elif defined(HAVE_GETPAGESIZE)
    pgsz = (size_t) getpagesize();
#endif
    if(pgsz > 0)
	return (size_t) pgsz;
    return (size_t)S3IO_DEFAULT_PAGESIZE;
}

/*
 * What is the preferred I/O block size?
 */
static size_t
blksize()
{
    return (size_t) 2 * pagesize();
}

/* End OS */

/* Begin s3 */

/* Write out a "page" of data to the file. The size of the page
   (i.e. the extent) varies.

   nciop - pointer to the file metadata.
   offset - where in the file should this page be written.
   extent - how many bytes should be written.
   vp - pointer to the data to write.
   posp - pointer to current position in file, updated after write.
*/
static int
s3_pgout(ncio* const nciop,
         off_t const offset,  const size_t extent,
         void *const vp, off_t *posp)
{
    int stat = ENOERR;
    ncio_s3* ns3 = (ncio_s3*)nciop->pvt;
    S3* s3 = ns3->s3;

#ifdef X_ALIGN
    assert((offset % X_ALIGN) == 0);
#endif
    assert(*posp == OFF_NONE || *posp == lseek(nciop->fd, 0, SEEK_CUR));
    *posp = offset;
    stat = ls3_write_data(s3,vp,offset,extent);    
    if(stat)
	return stat;
    *posp += extent;
    return ENOERR;
}

/*! Read in a page of data.

  @param[in] nciop  A pointer to the ncio struct for this file.
  @param[in] offset The byte offset in file where read starts.
  @param[in] extent The size of the page that will be read.
  @param[in] vp     A pointer to where the data will end up.

  @param[in,out] nreadp Returned number of bytes actually read (may be less than extent).
  @param[in,out] posp The pointer to current position in file, updated after read.
  @return Return 0 on success, otherwise an error code.
*/
static int
s3_pgin(ncio* const nciop,
        off_t const offset, const size_t extent,
        void *const vp, size_t *nreadp, off_t *posp)
{
    int status = ENOERR;
    ncio_s3* ns3 = (ncio_s3*)nciop->pvt;
    S3* s3 = ns3->s3;
    size_t nread = 0;

#ifdef X_ALIGN
    assert((offset % X_ALIGN) == 0);
    assert((extent % X_ALIGN) == 0);
#endif
    *posp = offset;
    errno = 0;
    status = ls3_read_data(s3,vp,offset,extent);
    if(status)
	return status;
    nread = ls3_get_iocount(s3);
    if(nread < extent) /* it's okay we read less than asked for */
	(void) memset((char *)vp + nread, 0, (ssize_t)extent - nread);
    *nreadp = nread;
    *posp += nread;
    return ENOERR;
}

/* This function indicates the file region starting at offset may be
   released.
   If called with RGN_MODIFIED
   flag, sets the modified flag in s3p->bf_rflags and decrements the
   reference count.

   s3p - pointer to posix non-share ncio_s3 struct.
   offset - file offset for beginning of to region to be released.
   rflags - only RGN_MODIFIED is relevent to this function, others ignored
*/
static int
s3_rel(ncio_s3* const s3p, off_t offset, int rflags)
{
    int status = ENOERR;
    s3_buffer* buf = &s3p->buf;

    assert(buf->bf_offset <= offset
           && offset < buf->bf_offset + (off_t) buf->bf_extent);
    assert(pIf(fIsSet(rflags, RGN_MODIFIED),
           fIsSet(buf->bf_rflags, RGN_WRITE)));

    if(fIsSet(rflags, RGN_MODIFIED)) {
	fSet(buf->bf_rflags, RGN_MODIFIED);
    }
    buf->bf_refcount--;
    return ENOERR;
}

/* This function indicates the file region starting at offset may be
   released.  Each read or write to the file is bracketed by a call to
   the "get" region function and a call to the "rel" region function.
   If you only read from the memory region, release it with a flag of
   0, if you modify the region, release it with a flag of
   RGN_MODIFIED.

   For POSIX system, without NC_SHARE, this becomes the rel function
   pointed to by the ncio rel function pointer. It mearly checks for
   file write permission, then calls s3_rel to do everything.

   nciop - pointer to ncio struct.
   offset - num bytes from beginning of buffer to region to be
   released.
   rflags - only RGN_MODIFIED is relevent to this function, others ignored
*/
static int
ncio_s3_rel(ncio* const nciop, off_t offset, int rflags)
{
    ncio_s3* const s3p = (ncio_s3*)nciop->pvt;

    if(fIsSet(rflags, RGN_MODIFIED) && !fIsSet(nciop->ioflags, NC_WRITE))
	return EPERM; /* attempt to write readonly file */
    return s3_rel(s3p, offset, rflags);
}

/* POSIX get. This will "make a region available." Since we're using
   buffered IO, this means that if needed, we'll fetch a new page from
   the file, otherwise, just return a pointer to what's in memory
   already.

   nciop - pointer to ncio struct, containing file info.
   s3p - pointer to ncio_s3 struct, which contains special metadate
   for posix files without NC_SHARE.
   offset - start byte of region to get.
   extent - how many bytes to read.
   rflags - One of the RGN_* flags defined in ncio.h.
   vpp - pointer to pointer that will recieve data.

   NOTES:

   * For blkoffset round offset down to the nearest s3p->blksz. This
   provides the offset (in bytes) to the beginning of the block that
   holds the current offset.

   * diff tells how far into the current block we are.

   * For blkextent round up to the number of bytes at the beginning of
   the next block, after the one that holds our current position, plus
   whatever extra (i.e. the extent) that we are about to grab.

   * The blkextent can't be more than twice the s3p->blksz. That's
   because the s3p->blksize is the sizehint, and in ncio_s3_init2 the
   buffer (pointed to by s3p->bf-base) is allocated with 2 *
   *sizehintp. This is checked (unneccesarily) more than once in
   asserts.

   * If this is called on a newly opened file, s3p->bf_offset will be
   OFF_NONE and we'll jump to label pgin to immediately read in a
   page.
*/
static int
s3_get(ncio* const nciop, ncio_s3 *const ns3,
       off_t offset, size_t extent,
       int rflags, void **const vpp)
{
    int status = ENOERR;
    s3_buffer* s3p = &ns3->buf;
    S3* s3 = ns3->s3;

    const off_t blkoffset = _RNDDOWN(offset, (off_t)s3p->blksz);
    off_t diff = (size_t)(offset - blkoffset);
    off_t blkextent = _RNDUP(diff + extent, s3p->blksz);

    assert(extent != 0);
    assert(extent < X_INT_MAX); /* sanity check */
    assert(offset >= 0); /* sanity check */

    if(2 * s3p->blksz < blkextent)
	return E2BIG; /* TODO: temporary kludge */
    if(s3p->bf_offset == OFF_NONE) {/* Uninitialized */
	if(s3p->bf_base == NULL) {
	    assert(s3p->bf_extent == 0);
            assert(blkextent <= 2 * s3p->blksz);
	    s3p->bf_base = malloc(2 * s3p->blksz);
            if(s3p->bf_base == NULL)
		return ENOMEM;
        }
        goto pgin;
    } /* else */
        assert(blkextent <= 2 * s3p->blksz);

    if(blkoffset == s3p->bf_offset) {/* hit */
	if(blkextent > s3p->bf_extent) {/* page in upper */
	    void *const middle = (void *)((char *)s3p->bf_base + s3p->blksz);
	    assert(s3p->bf_extent == s3p->blksz);
            status = s3_pgin(nciop,
                             s3p->bf_offset + (off_t)s3p->blksz,
                             s3p->blksz,
                             middle,
                             &s3p->bf_cnt,
                             &s3p->pos);
	    if(status != ENOERR)
		return status;
            s3p->bf_extent = 2 * s3p->blksz;
            s3p->bf_cnt += s3p->blksz;
        }
        goto done;
    } /* else */
    if(s3p->bf_extent > s3p->blksz
       && blkoffset == s3p->bf_offset + (off_t)s3p->blksz) { /* hit in upper half */
	if(blkextent == s3p->blksz) {/* all in upper half, no fault needed */
	    diff += s3p->blksz;
            goto done;
        } /* else */
        if(s3p->bf_cnt > s3p->blksz) { /* data in upper half */
	    void *const middle = (void *)((char *)s3p->bf_base + s3p->blksz);
	    assert(s3p->bf_extent == 2 * s3p->blksz);
            if(fIsSet(s3p->bf_rflags, RGN_MODIFIED)) { /* page out lower half */
		assert(s3p->bf_refcount <= 0);
                status = s3_pgout(nciop,
                                    s3p->bf_offset,
                                    s3p->blksz,
                                    s3p->bf_base,
                                    &s3p->pos);
                if(status != ENOERR)
		    return status;
            }
	    s3p->bf_cnt -= s3p->blksz;
	    /* copy upper half into lower half */
	    (void) memcpy(s3p->bf_base, middle, s3p->bf_cnt);
        } else { /* added to fix nofill bug */
	    assert(s3p->bf_extent == 2 * s3p->blksz);
            /* still have to page out lower half, if modified */
            if(fIsSet(s3p->bf_rflags, RGN_MODIFIED)) {
		assert(s3p->bf_refcount <= 0);
                status = s3_pgout(nciop,
                                    s3p->bf_offset,
                                    s3p->blksz,
                                    s3p->bf_base,
                                    &s3p->pos);
                if(status != ENOERR)
		    return status;
            }
        }
	s3p->bf_offset = blkoffset;
        /* s3p->bf_extent = s3p->blksz; */
        assert(blkextent == 2 * s3p->blksz);
        {
	    /* page in upper */
            void *const middle =
		(void *)((char *)s3p->bf_base + s3p->blksz);
            status = s3_pgin(nciop,
                             s3p->bf_offset + (off_t)s3p->blksz,
                             s3p->blksz,
                             middle,
                             &s3p->bf_cnt,
                             &s3p->pos);
	    if(status != ENOERR)
		return status;
            s3p->bf_extent = 2 * s3p->blksz;
            s3p->bf_cnt += s3p->blksz;
        }
	goto done;
    } /* else */
    if(blkoffset == s3p->bf_offset - (off_t)s3p->blksz) {
	/* wants the page below */
        void *const middle = (void *)((char *)s3p->bf_base + s3p->blksz);
	size_t upper_cnt = 0;
        if(s3p->bf_cnt > s3p->blksz) {
	    /* data in upper half */
	    assert(s3p->bf_extent == 2 * s3p->blksz);
            if(fIsSet(s3p->bf_rflags, RGN_MODIFIED)) {
		/* page out upper half */
                assert(s3p->bf_refcount <= 0);
                status = s3_pgout(nciop,
                                    s3p->bf_offset + (off_t)s3p->blksz,
                                    s3p->bf_cnt - s3p->blksz,
                                    middle,
                                    &s3p->pos);
                if(status != ENOERR)
		   return status;
	    }
	    s3p->bf_cnt = s3p->blksz;
            s3p->bf_extent = s3p->blksz;
        }
        if(s3p->bf_cnt > 0) {
	    /* copy lower half into upper half */
            (void) memcpy(middle, s3p->bf_base, s3p->blksz);
            upper_cnt = s3p->bf_cnt;
        }
        /* read page below into lower half */
        status = s3_pgin(nciop,
                     blkoffset,
                     s3p->blksz,
                     s3p->bf_base,
                     &s3p->bf_cnt,
                     &s3p->pos);
        if(status != ENOERR)
            return status;
        s3p->bf_offset = blkoffset;
        if(upper_cnt != 0) {
	    s3p->bf_extent = 2 * s3p->blksz;
            s3p->bf_cnt = s3p->blksz + upper_cnt;
        } else
	    s3p->bf_extent = s3p->blksz;
        goto done;
    }
    /* else */

    /* no overlap */
    if(fIsSet(s3p->bf_rflags, RGN_MODIFIED)) {
	assert(s3p->bf_refcount <= 0);
        status = s3_pgout(nciop,
                    s3p->bf_offset,
                    s3p->bf_cnt,
                    s3p->bf_base,
                    &s3p->pos);
        if(status != ENOERR)
            return status;
        s3p->bf_rflags = 0;
    }

pgin:
    status = s3_pgin(nciop,
             blkoffset,
             blkextent,
             s3p->bf_base,
             &s3p->bf_cnt,
             &s3p->pos);
    if(status != ENOERR)
	return status;
    s3p->bf_offset = blkoffset;
    s3p->bf_extent = blkextent;

done:
    extent += diff;
    if(s3p->bf_cnt < extent)
	s3p->bf_cnt = extent;
    assert(s3p->bf_cnt <= s3p->bf_extent);
    s3p->bf_rflags |= rflags;
    s3p->bf_refcount++;
    *vpp = (void *)((char *)s3p->bf_base + diff);
    return ENOERR;
}

/* Request that the region (offset, extent) be made available through
   *vpp.

   This function converts a file region specified by an offset and
   extent to a memory pointer. The region may be locked until the
   corresponding call to rel().

   For POSIX systems, without NC_SHARE. This function gets a page of
   size extent?

   This is a wrapper for the function s3_get, which does all the heavy
   lifting.

   nciop - pointer to ncio struct for this file.
   offset - offset (from beginning of file?) to the data we want to
   read.
   extent - the number of bytes to read from the file.
   rflags - One of the RGN_* flags defined in ncio.h.
   vpp - handle to point at data when it's been read.
*/
static int
ncio_s3_get(ncio* const nciop,
            off_t offset, size_t extent,
            int rflags,
            void **const vpp)
{
    ncio_s3 *const ns3 = (ncio_s3 *)nciop->pvt;
    s3_buffer* s3p = &ns3->buf;

    if(fIsSet(rflags, RGN_WRITE) && !fIsSet(nciop->ioflags, NC_WRITE))
        return EPERM; /* attempt to write readonly file */
    /* reclaim space used in move */
    if(ns3->slave != NULL) {
        if(ns3->slave->buf.bf_base != NULL) {
	    free(ns3->slave->buf.bf_base);
            ns3->slave->buf.bf_base = NULL;
            ns3->slave->buf.bf_extent = 0;
            ns3->slave->buf.bf_offset = OFF_NONE;
        }
	free(ns3->slave);
	ns3->slave = NULL;
    }
    return s3_get(nciop, ns3, offset, extent, rflags, vpp);
}

/* ARGSUSED */
static int
ncio_s3_double_buffer(ncio* const nciop, off_t to, off_t from,
                 size_t nbytes, int rflags)
{
    ncio_s3 *const ns3 = (ncio_s3 *)nciop->pvt;
    int status = ENOERR;
    void *src;
    void *dest;

#if INSTRUMENT
    fprintf(stderr, "\tdouble_buffr %ld %ld %ld\n",
             (long)to, (long)from, (long)nbytes);
#endif
    status = s3_get(nciop, ns3, to, nbytes, RGN_WRITE,
                    &dest);
    if(status != ENOERR)
	return status;

    if(ns3->slave == NULL) {

	ns3->slave = (ncio_s3 *) malloc(sizeof(ncio_s3));
        if(ns3->slave == NULL)
            return ENOMEM;
	ns3->slave->buf = ns3->buf;
	/* Override some fields */
        ns3->slave->buf.bf_base = malloc(2 * ns3->buf.blksz);
        if(ns3->slave->buf.bf_base == NULL)
	    return ENOMEM;
        (void) memcpy(ns3->slave->buf.bf_base, ns3->buf.bf_base, ns3->buf.bf_extent);
        ns3->slave->buf.bf_rflags = 0;
        ns3->slave->buf.bf_refcount = 0;
        ns3->slave->buf.slave = NULL;
    }
    ns3->slave->buf.pos = ns3->buf.pos;
    status = s3_get(nciop, ns3->slave, from, nbytes, 0, &src);
    if(status != ENOERR)
        return status;
    if(ns3->buf.pos != ns3->slave->buf.pos) {
	/* position changed, sync */
	ns3->buf.pos = ns3->slave->buf.pos;
    }
    (void) memcpy(dest, src, nbytes);
    (void)s3_rel(ns3->slave, from, 0);
    (void)s3_rel(ns3, to, RGN_MODIFIED);
    return status;
}

/* Flush any buffers to disk. May be a no-op on if I/O is unbuffered.
   This function is used when NC_SHARE is NOT used.
*/
static int
ncio_s3_sync(ncio* const nciop)
{
    ncio_s3 *const ns3 = (ncio_s3 *)nciop->pvt;
    int status = ENOERR;
    if(fIsSet(ns3->buf.bf_rflags, RGN_MODIFIED)) {
	assert(ns3->buf.bf_refcount <= 0);
        status = s3_pgout(nciop, ns3->buf.bf_offset,
                    ns3->buf.bf_cnt,
                    ns3->buf.bf_base, &ns3->buf.pos);
        if(status != ENOERR)
            return status;
        ns3->buf.bf_rflags = 0;
    } else if(!fIsSet(ns3->buf.bf_rflags, RGN_WRITE)) {
        /*
         * The dataset is readonly.  Invalidate the buffers so
         * that the next ncio_s3_get() will actually read data.
         */
	ns3->buf.bf_offset = OFF_NONE;
        ns3->buf.bf_cnt = 0;
    }
    return status;
}

/* Internal function called at close to
   free up anything hanging off pvt.
*/
static void
ncio_s3_freepvt(void *const pvt)
{
    ncio_s3 *const ns3 = (ncio_s3 *)pvt;

    if(ns3 == NULL)
	return;
    if(ns3->slave != NULL) {
	if(ns3->slave->buf.bf_base != NULL) {
	    free(ns3->slave->buf.bf_base);
            ns3->slave->buf.bf_base = NULL;
	    ns3->slave->buf.bf_extent = 0;
            ns3->slave->buf.bf_offset = OFF_NONE;
        }
        free(ns3->slave);
        ns3->slave = NULL;
    }
    if(ns3->buf.bf_base != NULL) {
	free(ns3->buf.bf_base);
        ns3->buf.bf_base = NULL;
        ns3->buf.bf_extent = 0;
        ns3->buf.bf_offset = OFF_NONE;
    }
}

/* This is the second half of the ncio initialization. This is called
   after the file has actually been opened.

   The most important thing that happens is the allocation of a block
   of memory at bf_base. This is going to be twice the size of
   the chunksizehint (rounded up to the nearest sizeof(double)) passed
   in from nc__create or nc__open. The rounded chunksizehint (passed
   in here in sizehintp) is going to be stored as blksize.

   According to our "contract" we are not allowed to ask for an extent
   larger than this chunksize/sizehint/blksize from the ncio get
   function.

   nciop - pointer to the ncio struct
   sizehintp - pointer to a size hint that will be rounded up and
   passed back to the caller.
   isNew - true if this is being called from ncio_create for a new
   file.
*/
static int
ncio_s3_init2(ncio* const nciop, size_t *sizehintp, int isNew)
{
    ncio_s3 *const ns3 = (ncio_s3 *)nciop->pvt;
    const size_t bufsz = 2 * *sizehintp;

    assert(nciop->fd >= 0);
    assert(ns3->buf.bf_base == NULL);

    ns3->buf.blksz = *sizehintp;
    /* this is separate allocation because it may grow */
    ns3->buf.bf_base = malloc(bufsz);
    if(ns3->buf.bf_base == NULL)
	return ENOMEM;
    /* else */
    ns3->buf.bf_cnt = 0;
    if(isNew) {
	/* save a read */
        ns3->buf.pos = 0;
        ns3->buf.bf_offset = 0;
        ns3->buf.bf_extent = bufsz;
        (void) memset(ns3->buf.bf_base, 0, ns3->buf.bf_extent);
    }
    return ENOERR;
}

/* This is the first of a two-part initialization of the ncio struct. */
static void
ncio_s3_init(ncio* const nciop)
{
    ncio_s3 *const ns3 = (ncio_s3 *)nciop->pvt;

    *((ncio_relfunc **)&nciop->rel) = ncio_s3_rel; /* cast away const */
    *((ncio_getfunc **)&nciop->get) = ncio_s3_get; /* cast away const */
    *((ncio_movefunc **)&nciop->move) = ncio_s3_move; /* cast away const */
    *((ncio_syncfunc **)&nciop->sync) = ncio_s3_sync; /* cast away const */
    *((ncio_filesizefunc **)&nciop->filesize) = ncio_s3_filesize; /* cast away const */
    *((ncio_pad_lengthfunc **)&nciop->pad_length) = ncio_s3_pad_length; /* cast away const */
    *((ncio_closefunc **)&nciop->close) = ncio_s3_close; /* cast away const */

    memset(&ns3->buf,0,sizeof(ns3->buf));
    ns3->buf.pos = -1;
    ns3->buf.bf_offset = OFF_NONE;
}

/* Copy one region to another without making anything available to
   higher layers. May be just implemented in terms of get() and rel(),
   or may be tricky to be efficient.  Only used in by nc_enddef()
   after redefinition.

   nciop - pointer to ncio struct for this file.
   to - dest for move?
   from - src for move?
   nbytes - number of bytes to move.
   rflags - One of the RGN_* flags defined in ncio.h.
*/
static int
ncio_s3_move(ncio* const nciop, off_t to, off_t from,
                    size_t nbytes, int rflags)
{
    int status = ENOERR;
    off_t lower = from;
    off_t upper = to;
    char *base;
    size_t diff;
    size_t extent;
    ncio_s3* nc3;
    S3error s3stat = S3_OK;
    nc3 = (ncio_s3*)nciop->pvt;

    if(fIsSet(rflags, RGN_WRITE) && !fIsSet(nciop->ioflags, NC_WRITE))
	return EPERM; /* attempt to write readonly file */
    rflags &= RGN_NOLOCK; /* filter unwanted flags */

    if(to == from)
	return ENOERR; /* NOOP */
    if(to > from) {/* growing */
	lower = from;
	upper = to;
    } else {/* shrinking */
	lower = to;
        upper = from;
    }
    diff = (size_t)(upper - lower);
    extent = diff + nbytes;
    if(extent > nc3->buf.blksz) {
	size_t remaining = nbytes;
	if(to > from) {
	    off_t frm = from + nbytes;
	    off_t toh = to + nbytes;
	    for(;;) {
		size_t loopextent = MIN(remaining, nc3->buf.blksz);
		frm -= loopextent;
		toh -= loopextent;
		status = ncio_s3_double_buffer(nciop, toh, frm,
	 			 	loopextent, rflags) ;
		if(status != ENOERR)
		    return status;
		remaining -= loopextent;
		if(remaining == 0)
		break; /* normal loop exit */
	    }
        } else {
	    for(;;) {
	        size_t loopextent = MIN(remaining, nc3->buf.blksz);
		status = ncio_s3_double_buffer(nciop, to, from,
				 	loopextent, rflags) ;
		if(status != ENOERR)
		    return status;
		remaining -= loopextent;
		if(remaining == 0)
		    break; /* normal loop exit */
		to += loopextent;
		from += loopextent;
	    }
	}
	return ENOERR;
    }

    status = ncio_s3_get(nciop, lower, extent, RGN_WRITE|rflags,
                         (void **)&base);
    if(status != ENOERR)
        return status;

    if(to > from)
	(void) memmove(base + diff, base, nbytes);
    else
        (void) memmove(base, base + diff, nbytes);
    (void) ncio_s3_rel(nciop, lower, RGN_MODIFIED);
    return status;
}

/* This will call whatever free function is attached to the free
   function pointer in ncio. It's called from ncio_close, and from
   ncio_open and ncio_create when an error occurs that the file
   metadata must be freed.
*/
static void
ncio_s3_free(ncio* nciop)
{
    if(nciop == NULL)
            return;
    if(nciop->pvt != NULL)
            ncio_s3_freepvt(nciop->pvt);
    free(nciop);
}

/* Create a new ncio struct to hold info about the file. This will
   create and init the ncio_s3 or ncio_s3 struct (the latter if
   NC_SHARE is used.)
*/
static ncio* 
ncio_s3_new(const char *path, int ioflags)
{
    ncio* nciop;

    nciop = (ncio* )calloc(1,sizeof(ncio));
    if(nciop == NULL)
	return NULL;
    nciop->path = strdup(path);
    (void) strcpy((char *)nciop->path, path); /* cast away const */
    /* cast away const */
    *((void **)&nciop->pvt) = (void *)calloc(1,sizeof(ncio_s3));
    ncio_s3_init(nciop);
    return nciop;
}

/* Public below this point */
#ifndef NCIO_MINBLOCKSIZE
#define NCIO_MINBLOCKSIZE 256
#endif

#ifndef NCIO_MAXBLOCKSIZE
#define NCIO_MAXBLOCKSIZE 268435456 /* sanity check, about X_SIZE_T_MAX/8 */
#endif

/* Create a file, and the ncio struct to go with it. This funtion is
   only called from nc__create_mp.

   path - path of file to create.
   cmode - mode flags from nc_create
   initialsz - From the netcdf man page: "The argument
   Iinitialsize sets the initial size of the file at creation time."
   igeto -
   igetsz -
   sizehintp - this eventually goes into s3p->blksz and is the size of
   a page of data for buffered reads and writes.
   nciopp - pointer to a pointer that will get location of newly
   created and inited ncio struct.
   igetvpp - pointer to pointer which will get the location of ?
*/
int
s3io_create(const char *path, int cmode,
    size_t initialsz,
    off_t igeto, size_t igetsz, size_t *sizehintp,
    void* parameters,
    ncio* *nciopp, void **const igetvpp)
{
    ncio* nciop;
    int status;
    S3error s3stat = S3_OK;
    ncio_s3* nc3;

    if(initialsz < (size_t)igeto + igetsz)
	initialsz = (size_t)igeto + igetsz;

    fSet(cmode, NC_WRITE);

    if(path == NULL || *path == 0)
	return EINVAL;

    if(fIsSet(cmode, NC_NOCLOBBER)) {
	S3* s3 = NULL;
        s3stat = ls3_open(path,&s3);
	if(s3stat && ls3_get_code(s3) == 200) {
	    ls3_close(s3);
	    /* exists */	    
	    s3stat = S3_EEXIST;
	    goto fail;
	}
    }

    nciop = ncio_s3_new(path, cmode);
    if(nciop == NULL)
	return ENOMEM;

    nc3 = (ncio_s3*)nciop->pvt;
    s3stat = ls3_create(path,&nc3->s3);
    if(s3stat) goto fail;

    if(*sizehintp < NCIO_MINBLOCKSIZE) {
	/* Use default */
        *sizehintp = blksize();
    } else if(*sizehintp >= NCIO_MAXBLOCKSIZE) {
	/* Use maximum allowed value */
        *sizehintp = NCIO_MAXBLOCKSIZE;
    } else {
	*sizehintp = M_RNDUP(*sizehintp);
    }
    status = ncio_s3_init2(nciop, sizehintp, 1);
    if(status != ENOERR)
	goto fail;

    if(igetsz != 0) {
	status = nciop->get(nciop, igeto, igetsz, RGN_WRITE, igetvpp);
        if(status != ENOERR)
	    goto fail;
    }
    *nciopp = nciop;
    return ENOERR;

fail:
    if(nciop)
        ncio_close(nciop,!fIsSet(cmode, NC_NOCLOBBER));
    if(!status)
	status = errcvt(s3stat);
    return status;
}


/* This function opens the data file. It is only called from nc.c,
   from nc__open_mp and nc_delete_mp.

   path - path of data file.

   cmode - mode flags passed into nc_open.

   igeto - looks like this function can do an initial page get, and
   igeto is going to be the offset for that. But it appears to be
   unused

   igetsz - the size in bytes of initial page get (a.k.a. extent). Not
   ever used in the library.

   sizehintp - pointer to sizehint parameter from nc__open or
   nc__create. This is used to set s3p->blksz.

   Here's what the man page has to say:

   "The argument referenced by chunksize controls a space versus time
   tradeoff, memory allocated in the netcdf library versus number of
   system calls.

   Because of internal requirements, the value may not be set to
   exactly the value requested. The actual value chosen is returned by reference.

   Using the value NC_SIZEHINT_DEFAULT causes the library to choose a
   default. How the system choses the default depends on the
   system. On many systems, the "preferred I/O block size" is
   available from the stat() system call, struct stat member
   st_blksize. If this is available it is used. Lacking that, twice
   the system pagesize is used. Lacking a call to discover the system
   pagesize, we just set default chunksize to 8192.

   The chunksize is a property of a given open netcdf descriptor ncid,
   it is not a persistent property of the netcdf dataset."

   nciopp - pointer to pointer that will get address of newly created
   and inited ncio struct.

   igetvpp - handle to pass back pointer to data from inital page
   read, if this were ever used, which it isn't.
*/
int
s3io_open(const char *path,
    int cmode,
    off_t igeto, size_t igetsz, size_t *sizehintp,
    void* parameters,
    ncio* *nciopp, void **const igetvpp)
{
    ncio* nciop;
    int status = 0;
    S3error s3stat = S3_OK;
    ncio_s3* nc3;

    if(path == NULL || *path == 0)
	return EINVAL;

    nciop = ncio_s3_new(path, cmode);
    if(nciop == NULL)
	return ENOMEM;

    nc3 = (ncio_s3*)nciop->pvt;
    s3stat = ls3_open(path,&nc3->s3);
    if(s3stat) goto fail;

    if(*sizehintp < NCIO_MINBLOCKSIZE) {
	/* Use default */
        *sizehintp = blksize();
    } else if(*sizehintp >= NCIO_MAXBLOCKSIZE) {
	/* Use maximum allowed value */
        *sizehintp = NCIO_MAXBLOCKSIZE;
    } else {
	*sizehintp = M_RNDUP(*sizehintp);
    }

    status = ncio_s3_init2(nciop, sizehintp, 0);

    if(status != ENOERR)
	goto fail;

    if(igetsz != 0) {
	status = nciop->get(nciop,
                            igeto, igetsz,
                            0,
                            igetvpp);
	if(status != ENOERR)
	    goto fail;
    }

    *nciopp = nciop;
    return ENOERR;

fail:
    if(nciop)
        ncio_close(nciop,!fIsSet(cmode, NC_NOCLOBBER));
    if(!status)
	status = errcvt(s3stat);
    return status;
}

/*
 * Get file size in bytes.
 */
static int
ncio_s3_filesize(ncio* nciop, off_t *filesizep)
{


    /* There is a problem with fstat on Windows based systems
            which manifests (so far) when Config RELEASE is built.
            Use _filelengthi64 isntead. */
#ifdef HAVE_FILE_LENGTH_I64

    __int64 file_len = 0;
    if( (file_len = _filelengthi64(nciop->fd)) < 0) {
            return errno;
    }

    *filesizep = file_len;

#else
struct stat sb;
assert(nciop != NULL);
if(fstat(nciop->fd, &sb) < 0)
    return errno;
*filesizep = sb.st_size;
#endif
    return ENOERR;
}

/*
 * Sync any changes to disk, then truncate or extend file so its size
 * is length.  This is only intended to be called before close, if the
 * file is open for writing and the actual size does not match the
 * calculated size, perhaps as the result of having been previously
 * written in NOFILL mode.
 */
static int
ncio_s3_pad_length(ncio* nciop, off_t length)
{

    int status = ENOERR;
    if(nciop == NULL)
	return EINVAL;

    if(!fIsSet(nciop->ioflags, NC_WRITE))
	return EPERM; /* attempt to write readonly file */
    status = nciop->sync(nciop);
    if(status != ENOERR)
	return status;
    return ENOERR;
}


/* Write out any dirty buffers to disk and
   ensure that next read will get data from disk.

   Sync any changes, then close the open file associated with the ncio
   struct, and free its memory.

   nciop - pointer to ncio to close.

   doUnlink - if true, unlink file
*/
static int
ncio_s3_close(ncio* nciop, int doUnlink)
{
    int status = ENOERR;
    char* path = NULL;
    ncio_s3* ns3 = (ncio_s3*)nciop->pvt;

    if(nciop == NULL)
            return EINVAL;
    (void) nciop->sync(nciop);

    if(doUnlink)
	path = strdup(nciop->path);
    (void) ls3_close(ns3->s3);
    ncio_s3_free(nciop);
    if(doUnlink) {
	(void) ls3_delete(path);
	if(path) free(path);
    }
    return status;
}

