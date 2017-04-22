/*! \file netcdf_mem.h
 *
 * Main header file for in-memory (diskless) functionality.
 *
 * Copyright 2010 University Corporation for Atmospheric
 * Research/Unidata. See COPYRIGHT file for more info.
 *
 * See \ref copyright file for more info.
 *
 */

#ifndef NETCDF_MEM_H
#define NETCDF_MEM_H 1

#if defined(__cplusplus)
extern "C" {
#endif

/* Declaration modifiers for DLL support (MSC et al) */
#if defined(DLL_NETCDF) /* define when library is a DLL */
#  if defined(DLL_EXPORT) /* define when building the library */
#   define MSC_EXTRA __declspec(dllexport)
#  else
#   define MSC_EXTRA __declspec(dllimport)
#  endif
#include <io.h>
#else
#define MSC_EXTRA
#endif	/* defined(DLL_NETCDF) */

# define EXTERNL MSC_EXTRA extern

EXTERNL int nc_def_var_filter(int ncid, int varid, unsigned int id, size_t nparams, const unsigned int* parms);

#if defined(__cplusplus)
}
#endif

#endif /* NETCDF_MEM_H */
