/*********************************************************************
*    Copyright 2018, UCAR/Unidata
*    See netcdf/COPYRIGHT file for copying and redistribution conditions.
* ********************************************************************/

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <hdf5.h>
#include "nc4internal.h"
#include <H5DSpublic.h> /* must be after nc4internal.h */
#include <H5Fpublic.h>
#include <H5LTpublic.h>

int
NC4_open_image_file(NC_HDF5_FILE_INFO_T* h5, hid_t* hdfidp)
{
    int stat = NC_NOERR;
    hid_t hdfid;
    unsigned int flags = 0;

    /* check arguments */
    if(h5->memio.memory == NULL || h5->memio.size == 0)
	{stat = NC_EINVAL; goto done;}

    /* Figure out the flags */
    if((h5->memio.flags & NC_MEMIO_LOCKED) == NC_MEMIO_LOCKED) {
	flags = (H5LT_FILE_IMAGE_DONT_COPY | H5LT_FILE_IMAGE_DONT_RELEASE);
    }
    /* Create the file */
    hdfid = H5LTopen_file_image(h5->memio.memory,h5->memio.size,flags);
    if(hdfid < 0)
	{stat = NC_EHDFERR; goto done;}

    /* Return file identifier */ 
    if(hdfidp) *hdfidp = hdfid;

done:
    return stat;
}

int
NC4_create_image_file(NC_HDF5_FILE_INFO_T* h5, hid_t* hdfidp)
{
    int stat = NC_NOERR;
    NC* nc = h5->controller;
    hid_t		fcpl, fapl, file_id;	/* HDF5 identifiers */
    size_t              alloc_incr;     /* Buffer allocation increment */
    size_t              min_incr = 65536; /* Minimum buffer increment */
    double              buf_prcnt = 0.1f;  /* Percentage of buffer size to set
                                             as increment */

    /* Assume that the default buffer size and memory were set by caller */
    if(h5->memio.memory == NULL || h5->memio.size == 0)
	{stat = NC_EINVAL; goto done;}

    /* Make sure initial buffer is clear */
    memset(h5->memio.memory,0,h5->memio.size);

    /* Create FAPL to transmit file image */
    if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) < 0) 
        {stat = NC_EHDFERR; goto done;}

    /* Make fcpl be default for now */
    fcpl = H5P_DEFAULT;

    /* set allocation increment to a percentage of the supplied buffer mem, or
     * a pre-defined minimum increment value, whichever is larger
     */
    if ((buf_prcnt * h5->memio.size) > min_incr)
        alloc_incr = (size_t)(buf_prcnt * h5->memio.size);
    else
        alloc_incr = min_incr;

    /* Configure FAPL to use the core file driver with no backing store */
    if (H5Pset_fapl_core(fapl, alloc_incr, 0) < 0) 
        {stat = NC_EHDFERR; goto done;}

    /* Assign file image in user buffer to FAPL */
    if(H5Pset_file_image(fapl, h5->memio.memory, h5->memio.size) < 0) 
        {stat = NC_EHDFERR; goto done;}

    /* Create the file */
    if((file_id = H5Fcreate(nc->path, H5F_ACC_TRUNC, fcpl, fapl)) < 0) 
        {stat = NC_EHDFERR; goto done;}

    /* Return file identifier */ 
    if(hdfidp) *hdfidp = file_id;

done:
    /* Close FAPL */
    H5Pclose(fapl);
    return stat;
}

int
NC4_extract_file_image(NC_HDF5_FILE_INFO_T* h5)
{
    int stat = NC_NOERR;
    hid_t hdfid;
    ssize_t size;

    assert(h5 && !h5->no_write);
    hdfid = h5->hdfid;    

    /* Pass 1: get the memory chunk size */
    size = H5Fget_file_image(hdfid, NULL, 0);
    if(size < 0)
        {stat = NC_EHDFERR; goto done;}
    /* Create the space to hold image */
    h5->memio.size = (size_t)size;
    h5->memio.memory = malloc(h5->memio.size);
    if(h5->memio.memory == NULL)
        {stat = NC_ENOMEM; goto done;}
    /* Pass 2: get the memory chunk itself */
    size = H5Fget_file_image(hdfid, h5->memio.memory, h5->memio.size);
    if(size < 0)
        {stat = NC_EHDFERR; goto done;}
done:
    return stat;

}
