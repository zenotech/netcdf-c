/**
This software is released under the terms of the Apache License version 2.
For details of the license, see http://www.apache.org/licenses/LICENSE-2.0.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>

#include "hdf5.h"

#include "libs3.h"

#define MAXADDR (((haddr_t)1<<(8*sizeof(off_t)-1))-1)
#define ADDR_OVERFLOW(A)	(HADDR_UNDEF==(A) ||			      \
				 ((A) & ~(haddr_t)MAXADDR))
#define SIZE_OVERFLOW(Z)	((Z) & ~(hsize_t)MAXADDR)
#define REGION_OVERFLOW(A,Z)	(ADDR_OVERFLOW(A) || SIZE_OVERFLOW(Z) ||      \
                                 HADDR_UNDEF==(A)+(Z) ||		      \
				 (off_t)((A)+(Z))<(off_t)(A))

/* HDF5 File Driver API*/
static H5FD_t* H5FD_s3_open(const char* name, unsigned flags, hid_t fapl_id, haddr_t maxaddr);
static herr_t H5FD_s3_close(H5FD_t* _file);
static int H5FD_s3_cmp(const H5FD_t* _f1, const H5FD_t* _f2);
static herr_t H5FD_s3_query(const H5FD_t* _f1, unsigned long* flags);
static haddr_t H5FD_s3_get_eoa(const H5FD_t* _file, H5FD_mem_t);
static herr_t H5FD_s3_set_eoa(H5FD_t* _file, H5FD_mem_t, haddr_t addr);
static haddr_t H5FD_s3_get_eof(const H5FD_t* _file);
static herr_t  H5FD_s3_get_handle(H5FD_t* _file, hid_t fapl, void** file_handle);
static herr_t H5FD_s3_read(H5FD_t* _file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size, void* buf);
static herr_t H5FD_s3_write(H5FD_t* _file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size, const void* buf);
static herr_t H5FD_s3_flush(H5FD_t* _file, hid_t dxpl_id, unsigned closing);

/**************************************************/
/**
The description of a file belonging to this driver. The
'eoa' and 'eof' determine the amount of hdf5 address space
in use and the high-water mark of the file (the current size
of the underlying Unix file). The 'pos' value is used to
eliminate file position updates when they would be a
no-op. Unfortunately we've found systems that use separate
file position indicators for reading and writing so the
lseek can only be eliminated if the current operation is the
same as the previous operation.  When opening a file the
'eof' will be set to the current file size, 'eoa' will be
set to zero, 'pos' will be set to H5F_ADDR_UNDEF (as it is
when an error occurs), and 'op' will be set to
H5F_OP_UNKNOWN.
*/

typedef struct H5FD_s3_t {
    H5FD_t      pub;                    /*public stuff, must be first   */
    S3*         s3;                     /* s3 file info                 */
    haddr_t     eoa;                    /*end of allocated region       */
    haddr_t     eof;                    /*end of file; current file size*/
    haddr_t     pos;                    /*current file I/O position     */
    unsigned    write_access;   /* Flag to indicate the file was opened with write access */
} H5FD_s3_t;

/**************************************************/

/* The driver identification number*/
static hid_t H5FD_S3IO = 0;

static const H5FD_class_t H5FD_s3 = {
    "s3",					/*name			*/
    MAXADDR,					/*maxaddr		*/
    H5F_CLOSE_WEAK,				/* fc_degree		*/
    NULL,					/*sb_size		*/
    NULL,					/*sb_encode		*/
    NULL,					/*sb_decode		*/
    0, 						/*fapl_size		*/
    NULL,					/*fapl_get		*/
    NULL,					/*fapl_copy		*/
    NULL, 					/*fapl_free		*/
    0,						/*dxpl_size		*/
    NULL,					/*dxpl_copy		*/
    NULL,					/*dxpl_free		*/
    H5FD_s3_open,			        /*open			*/
    H5FD_s3_close,		                /*close			*/
    H5FD_s3_cmp,			        /*cmp			*/
    H5FD_s3_query,		                /*query			*/
    NULL,					/*get_type_map		*/
    NULL,					/* alloc 	        */
    NULL,	           			/*free			*/
    H5FD_s3_get_eoa,				/*get_eoa		*/
    H5FD_s3_set_eoa, 				/*set_eoa		*/
    H5FD_s3_get_eof,				/*get_eof		*/
    H5FD_s3_get_handle,                         /*get_handle            */
    H5FD_s3_read,				/*read			*/
    H5FD_s3_write,				/*write			*/
    H5FD_s3_flush,				/*flush			*/
    NULL,                                       /*truncate              */
    NULL,                                       /*lock                  */
    NULL,                                       /*unlock                */
    H5FD_FLMAP_DICHOTOMY			/*fl_map		*/
};

/**************************************************/
/* HDF5 File Driver Implementation*/

/*-------------------------------------------------------------------------
 * Function:	H5FD_s3_register
 *
 * Purpose:	Make sure this virtual file driver has been registered.
 *
 * Return:	void
 *
 *-------------------------------------------------------------------------
 */

static int registered = 0;

static void
H5FD_s3_register()
{
    hid_t herr = 0;
    static const char *func = "H5FD_s3_register"; /* Function Name for error reporting */
    herr = H5FDregister (&H5FD_s3);
    if(herr)
        H5Epush_ret(func, H5E_ERR_CLS, H5E_PLUGIN, H5E_VFL, "Could not register the S3 virtual file driver", NULL)
    else
        registered = 1;
}

/*-------------------------------------------------------------------------
 * Function:	H5Pset_fapl_s3
 *
 * Purpose:	Modify the file access property list to use the H5FD_S3
 *		driver defined in this source file.  There are no driver
 *		specific properties.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pset_fapl_s3(hid_t fapl_id)
{
    herr_t ret_value;
    static const char *func = "H5FD_fapl_s3"; /* Function Name for error reporting */

    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);

    if(0 == H5Pisa_class(fapl_id, H5P_FILE_ACCESS))
        H5Epush_ret(func, H5E_ERR_CLS, H5E_PLIST, H5E_BADTYPE, "not a file access property list", -1)

    /* Make sure that our driver has been registered */
    if(!registered) {
        H5FD_s3_register();
    }

    return H5Pset_driver(fapl_id, H5FD_S3IO, NULL);
}

/*-------------------------------------------------------------------------
 * Function:  H5FD_stdio_open
 *
 * Purpose:  Create and/or opens a Standard C file as an HDF5 file.
 *
 * Errors:
 *  IO  CANTOPENFILE    File doesn't exist and CREAT wasn't
 *                      specified.
 *  IO  CANTOPENFILE    fopen() failed.
 *  IO  FILEEXISTS      File exists but CREAT and EXCL were
 *                      specified.
 *
 * Return:
 *      Success:    A pointer to a new file data structure. The
 *                  public fields will be initialized by the
 *                  caller, which is always H5FD_open().
 *
 *      Failure:    NULL
 *
 * Programmer: Dennis Heimbigner, Unidata
 *-------------------------------------------------------------------------
 */
static H5FD_t*
H5FD_s3_open(const char* name, unsigned flags, hid_t fapl_id, haddr_t maxaddr)
{
    unsigned write_access = 0; /* File opened with write access? */
    H5FD_s3_t* file = NULL;
    static const char *func = "H5FD_s3_open"; /* Function Name for error reporting */
    char* mode = "r";
    S3error s3stat = S3_OK;
    S3* s3 = NULL;
    int exists = 0;

    /* Quiet compiler */
    fapl_id = fapl_id;

    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);

    /* Check arguments */
    if (!name || !*name) {
        H5Epush_ret(func, H5E_ERR_CLS, H5E_ARGS, H5E_BADVALUE, "invalid file name", NULL)
	goto fail;
    }
    if (0 == maxaddr || HADDR_UNDEF == maxaddr) {
        H5Epush_ret(func, H5E_ERR_CLS, H5E_ARGS, H5E_BADRANGE, "bogus maxaddr", NULL)
	goto fail;
    }
    if (ADDR_OVERFLOW(maxaddr)) {
        H5Epush_ret(func, H5E_ERR_CLS, H5E_ARGS, H5E_OVERFLOW, "maxaddr too large", NULL)
	goto fail;
    }
    /* Tentatively open file in read-only mode, to check for existence */
    s3stat = ls3_open(name,"r",&s3);    
    if(s3stat == S3_OK && ls3_get_code(s3) == 200)
	exists = 1;
    (void)ls3_close(s3);

    if(flags & H5F_ACC_RDONLY) {
	if(!exists) {
	    H5Epush_ret(func, H5E_ERR_CLS, H5E_IO, H5E_CANTOPENFILE, "file doesn't exist and CREAT wasn't specified", NULL)
	    goto fail;
	}
	mode = "r";
	write_access = 0;
    } else if(flags & H5F_ACC_RDWR) {
	mode = ((flags & H5F_ACC_TRUNC) || (flags & H5F_ACC_CREAT) ? "w" : "rw");
	write_access = 1;
    }
    s3stat = ls3_open(name,"w",&s3);    
    if(s3stat) {
        H5Epush_ret(func, H5E_ERR_CLS, H5E_IO, H5E_CANTOPENFILE, "s3 open failed", NULL)
	goto fail;
    }   
    /* Build the return value */
    file = (H5FD_s3_t *)calloc((size_t)1, sizeof(H5FD_s3_t));
    if(file == NULL) {
        H5Epush_ret(func, H5E_ERR_CLS, H5E_RESOURCE, H5E_NOSPACE, "memory allocation failed", NULL)
	goto fail;
    }
    
    file->s3 = s3;
    file->eoa = 
    file->eof =
    file->pos = HADDR_UNDEF;
    file->write_access = write_access; /* Note the write_access for later */
    file->eof = ls3_get_eof(s3);
    file->eoa = HADDR_UNDEF;
    return (H5FD_t*)file;
fail:
    return NULL;
}
   
/*-------------------------------------------------------------------------
 * Function:  H5F_stdio_close
 *
 * Purpose:  Closes a file.
 *
 * Errors:
 *    IO    CLOSEERROR  Fclose failed.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_s3_close(H5FD_t* _file)
{
    H5FD_s3_t* file = (H5FD_s3_t*)_file;
    static const char *func = "H5FD_s3_close";  /* Function Name for error reporting */

    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);

    if(ls3_close(file->s3))
        H5Epush_ret(func, H5E_ERR_CLS, H5E_IO, H5E_CLOSEERROR, "fclose failed", -1)
    free(file);
    return 0;
}
   
/*-------------------------------------------------------------------------
 * Function:  H5FD_s3_cmp
 *
 * Purpose:  Compares two files belonging to this driver using an
 *    arbitrary (but consistent) ordering.
 *
 * Return:
 *      Success:    A value like strcmp()
 *
 *      Failure:    never fails (arguments were checked by the caller).
 *
 *-------------------------------------------------------------------------
 */
static int
H5FD_s3_cmp(const H5FD_t* _f1, const H5FD_t* _f2)
{
    H5FD_s3_t* f1 = (H5FD_s3_t*)_f1;
    H5FD_s3_t* f2 = (H5FD_s3_t*)_f2;

    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);

    return strcmp(ls3_get_s3url(f1->s3),ls3_get_s3url(f2->s3));
}
   
/*-------------------------------------------------------------------------
 * Function:  H5FD_s3_query
 *
 * Purpose:  Set the flags that this VFL driver is capable of supporting.
 *              (listed in H5FDpublic.h)
 *
 * Return:  Success:  non-negative
 *
 *    Failure:  negative
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_s3_query(const H5FD_t* _f1, unsigned long* flags)
{
    H5FD_s3_t* f = (H5FD_s3_t*)_f1;

    if(flags) {
        *flags = 0;
        *flags|=H5FD_FEAT_AGGREGATE_METADATA; /* OK to aggregate metadata allocations */
        *flags|=H5FD_FEAT_ACCUMULATE_METADATA; /* OK to accumulate metadata for faster writes */
        *flags|=H5FD_FEAT_DATA_SIEVE;       /* OK to perform data sieving for faster raw data reads & writes */
        *flags|=H5FD_FEAT_AGGREGATE_SMALLDATA; /* OK to aggregate "small" raw data allocations */
    }
    return 0;
}
   
/*-------------------------------------------------------------------------
 * Function:  H5FD_s3_get_eoa
 *
 * Purpose:  Gets the end-of-address marker for the file. The EOA marker
 *           is the first address past the last byte allocated in the
 *           format address space.
 *
 * Return:  Success:  The end-of-address marker.
 *
 *    Failure:  HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD_s3_get_eoa(const H5FD_t* _file,  H5FD_mem_t ignore) 
{
    H5FD_s3_t* f = (H5FD_s3_t*)_file;

    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);
    return ls3_get_eof(f->s3);
}
   
/*-------------------------------------------------------------------------
 * Function:  H5FD_s3_set_eoa
 *
 * Purpose:  Set the end-of-address marker for the file. This function is
 *    called shortly after an existing HDF5 file is opened in order
 *    to tell the driver where the end of the HDF5 data is located.
 *
 * Return:  Success:  0
 *
 *    Failure:  Does not fail
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_s3_set_eoa(H5FD_t* _file, H5FD_mem_t ignore, haddr_t addr)
{
    H5FD_s3_t* f = (H5FD_s3_t*)_file;

    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);

    return 0;
}
   
/*-------------------------------------------------------------------------
 * Function:  H5FD_s3_get_eof
 *
 * Purpose:  Returns the end-of-file marker, which is the greater of
 *    either the Unix end-of-file or the HDF5 end-of-address
 *    markers.
 *
 * Return:  Success:  End of file address, the first address past
 *        the end of the "file", either the Unix file
 *        or the HDF5 file.
 *
 *    Failure:  HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD_s3_get_eof(const H5FD_t* _file)
{
    H5FD_s3_t* f = (H5FD_s3_t*)_file;
    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);
    return ls3_get_eof(f->s3);
}
   
/*-------------------------------------------------------------------------
 * Function:       H5FD_s3_get_handle
 *
 * Purpose:        Returns the file handle of s3 file driver.
 *
 * Returns:        Non-negative if succeed or negative if fails.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_s3_get_handle(H5FD_t* _file, hid_t fapl, void** file_handle)
{
    H5FD_s3_t* f = (H5FD_s3_t*)_file;
    static const char  *func = "H5FD_s3_get_handle";  /* Function Name for error reporting */
    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);
    *file_handle = &(f->s3);
    return 0;
}
   
/*-------------------------------------------------------------------------
 * Function:  H5FD_s3_read
 *
 * Purpose:  Reads SIZE bytes beginning at address ADDR in file LF and
 *    places them in buffer BUF.  Reading past the logical or
 *    physical end of file returns zeros instead of failing.
 *
 * Errors:
 *    IO    READERROR  fread failed.
 *    IO    SEEKERROR  fseek failed.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_s3_read(H5FD_t* _file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size, void* buf)
{
    H5FD_s3_t* file = (H5FD_s3_t*)_file;
    S3error s3stat = 0;
    static const char *func = "H5FD_s3_read";  /* Function Name for error reporting */
    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);
    if (HADDR_UNDEF==addr)
        H5Epush_ret (func, H5E_ERR_CLS, H5E_IO, H5E_OVERFLOW, "file address overflowed", -1)
    if (REGION_OVERFLOW(addr, size))
        H5Epush_ret (func, H5E_ERR_CLS, H5E_IO, H5E_OVERFLOW, "file address overflowed", -1)

    /* Check easy cases */
    if (0 == size)
        return 0;
    if ((haddr_t)addr >= file->eof) {
        memset(buf, 0, size);
        return 0;
    }
    /* Read zeros past the logical end of file (physical is handled below) */
    if (addr + size > file->eof) {
        size_t nbytes = (size_t) (addr + size - file->eof);
        memset((unsigned char *)buf + size - nbytes, 0, nbytes);
        size -= nbytes;
    }

    /* Read the data */
    s3stat = ls3_read_data(file->s3,buf,addr,size);
    if(s3stat)
        H5Epush_ret(func, H5E_ERR_CLS, H5E_IO, H5E_READERROR, "s3 read failed", -1)
    /* Update the file position data. */
    file->pos = addr;
    return 0;
}
   
/*-------------------------------------------------------------------------
 * Function:  H5FD_s3_write
 *
 * Purpose:  Writes SIZE bytes from the beginning of BUF into file LF at
 *    file address ADDR.
 *
 * Errors:
 *    IO    SEEKERROR   fseek failed.
 *    IO    WRITEERROR  fwrite failed.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 * Programmer:  Robb Matzke
 *    Wednesday, October 22, 1997
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_s3_write(H5FD_t* _file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size, const void* buf)
{
    H5FD_s3_t* file = (H5FD_s3_t*)_file;
    S3error s3stat = S3_OK;
    static const char *func = "H5FD_s3_write";  /* Function Name for error reporting */
    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);
    if (HADDR_UNDEF==addr)
        H5Epush_ret (func, H5E_ERR_CLS, H5E_IO, H5E_OVERFLOW, "file address overflowed", -1)
    if (REGION_OVERFLOW(addr, size))
        H5Epush_ret (func, H5E_ERR_CLS, H5E_IO, H5E_OVERFLOW, "file address overflowed", -1)

    /* write the data */
    s3stat = ls3_write_data(file->s3,buf,addr,size);
    if(s3stat)
        H5Epush_ret(func, H5E_ERR_CLS, H5E_IO, H5E_WRITEERROR, "s3 write failed", -1)
    /* Update the file position data. */
    file->pos = addr;
    /* Update EOF if necessary */
    if (file->pos > file->eof)
        file->eof = file->pos;
    return 0;
}
   
/*-------------------------------------------------------------------------
 * Function:  H5FD_s3_flush
 *
 * Purpose:  Makes sure that all data is on disk.
 *
 * Errors:
 *    IO    SEEKERROR     fseek failed.
 *    IO    WRITEERROR    fflush or fwrite failed.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_s3_flush(H5FD_t* _file, hid_t dxpl_id, unsigned closing)
{
    H5FD_s3_t* file = (H5FD_s3_t*)_file;
    static const char *func = "H5FD_s3_flush";  /* Function Name for error reporting */

    /* Quiet the compiler */
    dxpl_id = dxpl_id;

    /* Clear the error stack */
    H5Eclear2(H5E_DEFAULT);
    file->pos = HADDR_UNDEF;
    return 0;
}
