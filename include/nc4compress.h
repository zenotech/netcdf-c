#ifndef NC4COMPRESS_H
#define NC4COMPRESS_H

#include <hdf5.h>
#include "netcdf_compress.h"

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
