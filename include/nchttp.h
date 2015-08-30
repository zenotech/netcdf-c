/*********************************************************************
 *   Copyright 2010, UCAR/Unidata
 *   See netcdf/COPYRIGHT file for copying and redistribution conditions.
 *********************************************************************/

/* $Id: ncdispatch.h,v 1.18 2010/06/01 20:11:59 dmh Exp $ */
/* $Header: /upc/share/CVS/netcdf-3/libdispatch/ncdispatch.h,v 1.18 2010/06/01 20:11:59 dmh Exp $ */

#ifndef NCHTTP_H
#define NCHTTP_H

#include "config.h"
#include "netcdf.h"
#include "ncuri.h"

/* Does the path look like a url? */
extern int NC_testurl(const char* path);

/* Return model as specified by the url; NC_FORMATX_UNDEFINED if unknown */
extern int NC_urlmodel(const char* path, int* version);

#if 0
/* allow access url parse and params without exposing ncurl.h */
extern int NCDAP_urlparse(const char* s, void** dapurl);
extern void NCDAP_urlfree(void* dapurl);
extern const char* NCDAP_urllookup(void* dapurl, const char* param);
#endif

/* Ping a specific server */
extern int NC_ping(const char*);

/* Test for specific set of servers */
#if defined(DLL_NETCDF) /* Defined when library is a DLL */
# if defined(DLL_EXPORT) /* Define when building the library. */
#  define MSC_NCDISPATCH_EXTRA __declspec(dllexport)
# else
#  define MSC_NCDISPATCH_EXTRA __declspec(dllimport)
# endif
MSC_NCDISPATCH_EXTRA extern char* NC_findtestserver(const char*, const char**);
MSC_NCDISPATCH_EXTRA extern int nc_open_mem(const char*, int, size_t, void*, int*);
#else
extern char* NC_findtestserver(const char*,const char**);
#endif

/**
Provide a way for modules to register itself
so that it can be used to convert a url to a model.
*/

/* Expected callback signature */
typedef int (*NC_protocol_test)(int dfalt, NCURI* url, int* model, int* version);

/* Registry function: first means insert into front of list */
extern int NC_register_protocol(NC_protocol_test callback, int dfalt);

#endif /*NCHTTP_H*/
