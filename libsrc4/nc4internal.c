/** \file \internal
Internal netcdf-4 functions.

This file contains functions internal to the netcdf4 library. None of
the functions in this file are exposed in the exetnal API. These
functions all relate to the manipulation of netcdf-4's in-memory
buffer of metadata information, i.e. the linked list of NC
structs.

Copyright 2003-2011, University Corporation for Atmospheric
Research. See the COPYRIGHT file for copying and redistribution
conditions.

*/
#include "config.h"
#include "nc4internal.h"
#include "nc.h" /* from libsrc */
#include "ncdispatch.h" /* from libdispatch */
#include "ncutf8.h"
#include "nclistmap.h"
#include "H5DSpublic.h"

#define MEGABYTE 1048576

#undef DEBUGH5

#ifdef DEBUGH5
/* Provide a catchable error reporting function */
static herr_t
h5catch(void* ignored)
{
    H5Eprint(NULL);
    return 0;
}
#endif


/* These are the default chunk cache sizes for HDF5 files created or
 * opened with netCDF-4. */
extern size_t nc4_chunk_cache_size;
extern size_t nc4_chunk_cache_nelems;
extern float nc4_chunk_cache_preemption;

/* This is to track opened HDF5 objects to make sure they are
 * closed. */
#ifdef EXTRA_TESTS
extern int num_spaces;
#endif /* EXTRA_TESTS */

#ifdef LOGGING
/* This is the severity level of messages which will be logged. Use
   severity 0 for errors, 1 for important log messages, 2 for less
   important, etc. */
int nc_log_level = NC_TURN_OFF_LOGGING;

#endif /* LOGGING */

int nc4_hdf5_initialized = 0;

/* Provide a wrapper for H5Eset_auto */
static herr_t
set_auto(void* func, void *client_data)
{
#ifdef DEBUGH5
    return H5Eset_auto2(H5E_DEFAULT,(H5E_auto2_t)h5catch,client_data);
#else
    return H5Eset_auto2(H5E_DEFAULT,(H5E_auto2_t)func,client_data);
#endif
}

/*
Provide a function to do any necessary initialization
of the HDF5 library.
*/

void
nc4_hdf5_initialize(void)
{
    if (set_auto(NULL, NULL) < 0)
	LOG((0, "Couldn't turn off HDF5 error messages!"));
    LOG((1, "HDF5 error messages have been turned off."));
    nc4_hdf5_initialized = 1;
}

/* Check and normalize and name. */
int
nc4_check_name(const char *name, char *norm_name)
{
   char *temp;
   int retval;

   /* Check the length. */
   if (strlen(name) > NC_MAX_NAME)
      return NC_EMAXNAME;

   /* Make sure this is a valid netcdf name. This should be done
    * before the name is normalized, because it gives better error
    * codes for bad utf8 strings. */
   if ((retval = NC_check_name(name)))
      return retval;

   /* Normalize the name. */
   retval = nc_utf8_normalize((const unsigned char *)name,(unsigned char**)&temp);
   if(retval != NC_NOERR)
      return retval;

   if(strlen(temp) > NC_MAX_NAME) {
     free(temp);
     return NC_EMAXNAME;
   }

   strcpy(norm_name, temp);
   free(temp);

   return NC_NOERR;
}

/* Given a varid, return the maximum length of a dimension using dimid */

static int
find_var_dim_max_length(NC_GRP_INFO_T *grp, int varid, int dimid, size_t *maxlen)
{
   hid_t datasetid = 0, spaceid = 0;
   NC_VAR_INFO_T *var;
   hsize_t *h5dimlen = NULL, *h5dimlenmax = NULL;
   int d, dataset_ndims = 0;
   int retval = NC_NOERR;

   *maxlen = 0;

   /* Find this var. */
   if (varid < 0 || varid >= NC_listmap_size(&grp->vars.value))
     return NC_ENOTVAR;
   var = NC_listmap_iget(&grp->vars.value,varid);
   if (!var) return NC_ENOTVAR;
   assert(var->varid == varid);

   /* If the var hasn't been created yet, its size is 0. */
   if (!var->created)
   {
     *maxlen = 0;
   }
   else
   {
     /* Get the number of records in the dataset. */
     if ((retval = nc4_open_var_grp2(grp, var->varid, &datasetid)))
       BAIL(retval);
     if ((spaceid = H5Dget_space(datasetid)) < 0)
       BAIL(NC_EHDFERR);
#ifdef EXTRA_TESTS
     num_spaces++;
#endif
     /* If it's a scalar dataset, it has length one. */
     if (H5Sget_simple_extent_type(spaceid) == H5S_SCALAR)
     {
       *maxlen = (var->dimids && var->dimids[0] == dimid) ? 1 : 0;
     }
     else
     {
       /* Check to make sure ndims is right, then get the len of each
	  dim in the space. */
       if ((dataset_ndims = H5Sget_simple_extent_ndims(spaceid)) < 0)
	 BAIL(NC_EHDFERR);
       if (dataset_ndims != var->ndims)
	 BAIL(NC_EHDFERR);
       if (!(h5dimlen = malloc(dataset_ndims * sizeof(hsize_t))))
	 BAIL(NC_ENOMEM);
       if (!(h5dimlenmax = malloc(dataset_ndims * sizeof(hsize_t))))
	 BAIL(NC_ENOMEM);
       if ((dataset_ndims = H5Sget_simple_extent_dims(spaceid,
						      h5dimlen, h5dimlenmax)) < 0)
	 BAIL(NC_EHDFERR);
       LOG((5, "find_var_dim_max_length: varid %d len %d max: %d",
	    varid, (int)h5dimlen[0], (int)h5dimlenmax[0]));
       for (d=0; d<dataset_ndims; d++) {
	 if (var->dimids[d] == dimid) {
	   *maxlen = *maxlen > h5dimlen[d] ? *maxlen : h5dimlen[d];
	 }
       }
     }
   }

  exit:
   if (spaceid > 0 && H5Sclose(spaceid) < 0)
      BAIL2(NC_EHDFERR);
#ifdef EXTRA_TESTS
   num_spaces--;
#endif
   if (h5dimlen) free(h5dimlen);
   if (h5dimlenmax) free(h5dimlenmax);
   return retval;
}

/* Given an NC pointer, add the necessary stuff for a
 * netcdf-4 file. */
int
nc4_nc4f_list_add(NC *nc, const char *path, int mode)
{
   NC_HDF5_FILE_INFO_T *h5;
   int ret = NC_NOERR;

   assert(nc && !NC4_DATA(nc) && path);

   /* We need to malloc and
      initialize the substructure NC_HDF_FILE_INFO_T. */
   if (!(h5 = calloc(1, sizeof(NC_HDF5_FILE_INFO_T))))
      return NC_ENOMEM;
   NC4_DATA_SET(nc,h5);
   h5->controller = nc;

   /* Hang on to cmode, and note that we're in define mode. */
   h5->cmode = mode | NC_INDEF;

   /* Establish the global type,dim, and group vectors */
   h5->alldims = nclistnew();
   h5->allgroups = nclistnew();
   h5->alltypes = nclistnew();
   /* The next_typeid needs to be set beyond the end of our atomic types. */
   { /* Add null entries for the atomic types */
      int i;
      for(i=0;i<NC_FIRSTUSERTYPEID;i++)
	nclistpush(h5->alltypes,NULL);
   }

   /* There's always at least one open group - the root
    * group. Allocate space for one group's worth of information. Set
    * its hdf id, name, and a pointer to it's file structure. */
   ret = nc4_grp_new(NULL, NC_GROUP_NAME, &h5->root_grp);
   if(ret == NC_NOERR)
     ret = nc4_grp_list_add(h5, h5->root_grp);
   return ret;
}

/* Given an ncid, find the relevant group and return a pointer to it,
 * return an error of this is not a netcdf-4 file (or if strict nc3 is
 * turned on for this file.) */
int
nc4_find_nc4_grp(int ncid, NC_GRP_INFO_T **grp)
{
   NC_HDF5_FILE_INFO_T* h5;
   NC *f = nc4_find_nc_file(ncid,&h5);
   if(f == NULL) return NC_EBADID;

   /* No netcdf-3 files allowed! */
   if (!h5) return NC_ENOTNC4;
   assert(h5->root_grp);

   /* This function demands netcdf-4 files without strict nc3
    * rules.*/
   if (h5->cmode & NC_CLASSIC_MODEL) return NC_ESTRICTNC3;

   /* If we can't find it, the grp id part of ncid is bad. */
   if (!(*grp = nc4_rec_find_grp(h5->root_grp, (ncid & GRP_ID_MASK))))
      return NC_EBADID;
   return NC_NOERR;
}

/* Given an ncid, find the relevant group and return a pointer to it,
 * also set a pointer to the nc4_info struct of the related file. For
 * netcdf-3 files, *h5 will be set to NULL. */
int
nc4_find_grp_h5(int ncid, NC_GRP_INFO_T **grpp, NC_HDF5_FILE_INFO_T **h5p)
{
    NC_HDF5_FILE_INFO_T *h5;
    NC_GRP_INFO_T *grp;
    NC *f = nc4_find_nc_file(ncid,&h5);
    if(f == NULL) return NC_EBADID;
    if (h5) {
        assert(h5->root_grp);
        /* If we can't find it, the grp id part of ncid is bad. */
	if (!(grp = nc4_rec_find_grp(h5->root_grp, (ncid & GRP_ID_MASK))))
  	    return NC_EBADID;
	h5 = (grp)->nc4_info;
	assert(h5);
    } else {
	h5 = NULL;
	grp = NULL;
    }
    if(h5p) *h5p = h5;
    if(grpp) *grpp = grp;
    return NC_NOERR;
}

int
nc4_find_nc_grp_h5(int ncid, NC **nc, NC_GRP_INFO_T **grpp,
		   NC_HDF5_FILE_INFO_T **h5p)
{
    NC_GRP_INFO_T *grp;
    NC_HDF5_FILE_INFO_T* h5;
    NC *f = nc4_find_nc_file(ncid,&h5);

    if(f == NULL) return NC_EBADID;
    *nc = f;

    if (h5) {
	assert(h5->root_grp);
	/* If we can't find it, the grp id part of ncid is bad. */
	if (!(grp = nc4_rec_find_grp(h5->root_grp, (ncid & GRP_ID_MASK))))
	       return NC_EBADID;

	h5 = (grp)->nc4_info;
	assert(h5);
    } else {
	h5 = NULL;
	grp = NULL;
    }
    if(h5p) *h5p = h5;
    if(grpp) *grpp = grp;
    return NC_NOERR;
}

/* Recursively hunt for a group id. */
NC_GRP_INFO_T *
nc4_rec_find_grp(NC_GRP_INFO_T *start_grp, int target_nc_grpid)
{
   NC_GRP_INFO_T *g, *res;

   assert(start_grp);

   /* Is this the group we are searching for? */
   if (start_grp->nc_grpid == target_nc_grpid)
      return start_grp;

   /* Shake down the kids. */
   if (NC_listmap_size(&start_grp->children) > 0) {
      size_t giter;
      for(giter=0;NC_listmap_next(&start_grp->children,giter,(uintptr_t*)&g);giter++)
	 if ((res = nc4_rec_find_grp(g, target_nc_grpid)))
 	    return res;
   }
   /* Can't find it. Fate, why do you mock me? */
   return NULL;
}

/* Given an ncid and varid, get pointers to the group and var
 * metadata. */
int
nc4_find_g_var_nc(NC *nc, int ncid, int varid,
		  NC_GRP_INFO_T **grp, NC_VAR_INFO_T **var)
{
   NC_HDF5_FILE_INFO_T* h5 = NC4_DATA(nc);

   /* Find the group info. */
   assert(grp && var && h5 && h5->root_grp);
   *grp = nc4_rec_find_grp(h5->root_grp, (ncid & GRP_ID_MASK));

   /* It is possible for *grp to be NULL. If it is,
      return an error. */
   if(*grp == NULL)
     return NC_ENOTVAR;

   /* Find the var info. */
   if (varid < 0 || varid >= NC_listmap_size(&((*grp)->vars.value)))
     return NC_ENOTVAR;
   (*var) = NC_listmap_iget(&((*grp)->vars.value),varid);

   return NC_NOERR;
}

/* Find a dim in a grp (or parents). */
int
nc4_find_dim(NC_GRP_INFO_T *grp, int dimid, NC_DIM_INFO_T **dimp,
	     NC_GRP_INFO_T **dim_grpp)
{
   NC_GRP_INFO_T *g, *dg = NULL;
   int finished = 0;
   NC_DIM_INFO_T* d;
   NC_HDF5_FILE_INFO_T* h5;
 
   assert(dim_grpp || dimp);

   /* Find the dim info. */
   
   /* First, find dim via dimid */
   h5 = grp->nc4_info;
   d = nclistget(h5->alldims,dimid);
   if(d == NULL)
      return NC_EBADDIM;
   /* Now search grp and its parents looking for this dim by name */
   dg = NULL;
   for (g = grp; g && !finished; g = g->parent) {
         NC_DIM_INFO_T* tmpd = NC_listmap_get(&g->dim,d->name);
      if(tmpd != NULL && tmpd->dimid == d->dimid) {
	dg = g;
	break;
      }
   }
   /* If we didn't find it, return an error. */
   if (dg == NULL)
     return NC_EBADDIM;

   if(dimp) *dimp = d;
   /* Give the caller the group the dimension is in. */
   if (dim_grpp)
      *dim_grpp = dg;

   return NC_NOERR;
}

/* Find a var (by name) in a grp. */
int
nc4_find_var(NC_GRP_INFO_T *grp, const char *name, NC_VAR_INFO_T **varp)
{
  int i;
  size_t iter;
  NC_VAR_INFO_T* var = NULL;

  assert(grp && varp && name);

   /* Find the var info. */
   *varp = NULL;
   for(iter=0;NC_listmap_next(&grp->vars.value,iter,(uintptr_t*)&var);iter++)
   {
     if (0 == strcmp(name, var->name))
     {
       *varp = var;
       break;
     }
   }
   return NC_NOERR;
}

/* Recursively hunt for a HDF type id. */
NC_TYPE_INFO_T *
nc4_rec_find_hdf_type(NC_GRP_INFO_T *start_grp, hid_t target_hdf_typeid)
{
   NC_GRP_INFO_T *g;
   NC_TYPE_INFO_T *type, *res;
   htri_t equal;
   size_t iter;

   assert(start_grp);

   /* Does this group have the type we are searching for? */
   for(iter=0;NC_listmap_next(&start_grp->type,iter,(uintptr_t*)&type);iter++)
   {
      if ((equal = H5Tequal(type->native_hdf_typeid ? type->native_hdf_typeid : type->hdf_typeid, target_hdf_typeid)) < 0)
	 return NULL;
      if (equal)
	 return type;
   }

   /* Shake down the kids. */
   if (NC_listmap_size(&start_grp->children) > 0) {
      size_t giter;
      for(giter=0;NC_listmap_next(&start_grp->children,giter,(uintptr_t*)&g);giter++) {
	 if ((res = nc4_rec_find_hdf_type(g, target_hdf_typeid)))
	    return res;
      }
   }
   /* Can't find it. Fate, why do you mock me? */
   return NULL;
}

/* Recursively hunt for a netCDF type by name. */
NC_TYPE_INFO_T *
nc4_rec_find_named_type(NC_GRP_INFO_T *start_grp, char *name)
{
   NC_GRP_INFO_T *g;
   NC_TYPE_INFO_T *type, *res;
   size_t iter;

   assert(start_grp);

   /* Does this group have the type we are searching for? */
   for(iter=0;NC_listmap_next(&start_grp->type,iter,(uintptr_t*)&type);iter++) {
      if (!strcmp(type->name, name))
	 return type;
   }

   /* Search subgroups. */
   if (NC_listmap_size(&start_grp->children) > 0) {
      for(iter=0;NC_listmap_next(&start_grp->children,iter,(uintptr_t*)&g);iter++) {
	 if ((res = nc4_rec_find_named_type(g, name)))
	    return res;
      }
   }
   /* Can't find it. Oh, woe is me! */
   return NULL;
}

/* Recursively hunt for a netCDF type id. */
NC_TYPE_INFO_T *
nc4_rec_find_nc_type(NC_GRP_INFO_T *start_grp, nc_type target_nc_typeid)
{
   NC_TYPE_INFO_T *type;
   size_t iter;

   assert(start_grp);

   /* Does this group have the type we are searching for? */
   for(iter=0;NC_listmap_next(&start_grp->type,iter,(uintptr_t*)&type);iter++) {
      if (type->nc_typeid == target_nc_typeid)
	 return type;
   }
   /* Shake down the kids. */
   if (NC_listmap_size(&start_grp->children) > 0)
   {
      NC_GRP_INFO_T *g;

      for(iter=0;NC_listmap_next(&start_grp->children,iter,(uintptr_t*)&g);iter++)
      {
         NC_TYPE_INFO_T *res;

	 if ((res = nc4_rec_find_nc_type(g, target_nc_typeid)))
	    return res;
      }
   }

   /* Can't find it. Fate, why do you mock me? */
   return NULL;
}

/* Use a netCDF typeid to find a type in a type_list. */
int
nc4_find_type(const NC_HDF5_FILE_INFO_T *h5, nc_type typeid, NC_TYPE_INFO_T **type)
{
   if (typeid < 0 || !type)
      return NC_EINVAL;
   *type = NULL;

   /* Atomic types don't have associated NC_TYPE_INFO_T struct, just
    * return NOERR. */
   if (typeid <= NC_STRING)
      return NC_NOERR;

   /* Find the type. */
   if(!(*type = nc4_rec_find_nc_type(h5->root_grp, typeid)))
      return NC_EBADTYPID;

   return NC_NOERR;
}

/* Find the actual length of a dim by checking the length of that dim
 * in all variables that use it, in grp or children. **len must be
 * initialized to zero before this function is called. */
int
nc4_find_dim_len(NC_GRP_INFO_T *grp, int dimid, size_t **len)
{
   NC_GRP_INFO_T *g;
   NC_VAR_INFO_T *var;
   int retval;
   int i;
   size_t iter;

   assert(grp && len);
   LOG((3, "nc4_find_dim_len: grp->name %s dimid %d", grp->name, dimid));

   /* If there are any groups, call this function recursively on
    * them. */
   for(iter=0;NC_listmap_next(&grp->children,iter,(uintptr_t*)&g);iter++) {
      if ((retval = nc4_find_dim_len(g, dimid, len)))
	 return retval;
   }

   /* For all variables in this group, find the ones that use this
    * dimension, and remember the max length. */
   for(iter=0;NC_listmap_next(&grp->vars.value,iter,(uintptr_t*)&var);iter++)
   {
     size_t mylen;
     if (!var) continue;

     /* Find max length of dim in this variable... */
     if ((retval = find_var_dim_max_length(grp, var->varid, dimid, &mylen)))
       return retval;

     **len = **len > mylen ? **len : mylen;
   }

   return NC_NOERR;
}

/* Given a group, find an att. */
int
nc4_find_grp_att(NC_GRP_INFO_T *grp, int varid, const char *name, int attnum,
		 NC_ATT_INFO_T **attp)
{
   NC_VAR_INFO_T *var;
   NC_listmap *attlist = NULL;
   NC_ATT_INFO_T* att;

   assert(grp && grp->name);
   LOG((4, "nc4_find_grp_att: grp->name %s varid %d name %s attnum %d",
	grp->name, varid, name, attnum));

   /* Get either the global or a variable attribute list. */
   if (varid == NC_GLOBAL)
      attlist = &grp->att;
   else
   {
      if (varid < 0 || varid >= NC_listmap_size(&grp->vars.value))
	return NC_ENOTVAR;
      var = NC_listmap_iget(&grp->vars.value,varid);
      if (!var) return NC_ENOTVAR;
      attlist = &var->att;
      assert(var->varid == varid);
   }

   /* Now find the attribute by name or number. If a name is provided,
    * ignore the attnum. */
   if(attlist) {
       size_t iter;
       for(iter=0;NC_listmap_next(attlist,iter,(uintptr_t*)&att);iter++) {
           if(attp) *attp = att;
           if (name && att->name && !strcmp(att->name, name))
	       return NC_NOERR;
           if (!name && att->attnum == attnum)
	       return NC_NOERR;
       }
   }
   /* If we get here, we couldn't find the attribute. */
   return NC_ENOTATT;
}

/* Given an ncid, varid, and name or attnum, find and return pointer
   to NC_ATT_INFO_T metadata. */
int
nc4_find_nc_att(int ncid, int varid, const char *name, int attnum,
	    NC_ATT_INFO_T **att)
{
   NC_GRP_INFO_T *grp;
   NC_HDF5_FILE_INFO_T *h5;
   NC_VAR_INFO_T *var;
   NC_listmap *attlist = NULL;
   int retval;
   size_t iter;

   LOG((4, "nc4_find_nc_att: ncid 0x%x varid %d name %s attnum %d",
	ncid, varid, name, attnum));

   /* Find info for this file and group, and set pointer to each. */
   if ((retval = nc4_find_grp_h5(ncid, &grp, &h5)))
      return retval;
   assert(grp && h5);

   /* Get either the global or a variable attribute list. */
   if (varid == NC_GLOBAL)
      attlist = &grp->att;
   else
   {
      if (varid < 0 || varid >= NC_listmap_size(&grp->vars.value))
	return NC_ENOTVAR;
      var = NC_listmap_iget(&grp->vars.value,varid);
      if (!var) return NC_ENOTVAR;
      attlist = &var->att;
      assert(var->varid == varid);
   }

   /* Now find the attribute by name or number. If a name is provided, ignore the attnum. */
   for(iter=0;NC_listmap_next(attlist,iter,(uintptr_t*)att);iter++) {
      if ((name && !strcmp((*att)->name, name)) ||
	  (!name && (*att)->attnum == attnum))
	 return NC_NOERR;
   }

   /* If we get here, we couldn't find the attribute. */
   return NC_ENOTATT;
}


/* Given an id, walk the list and find the appropriate
   NC. */
NC*
nc4_find_nc_file(int ext_ncid, NC_HDF5_FILE_INFO_T** h5p)
{
   NC* nc;
   int stat;

   stat = NC_check_id(ext_ncid,&nc);
   if(stat != NC_NOERR)
	nc = NULL;

   if(nc)
     if(h5p) *h5p = (NC_HDF5_FILE_INFO_T*)nc->dispatchdata;

   return nc;
}

/* Names for groups, variables, and types must not be the same. This
 * function checks that a proposed name is not already in
 * use. Normalzation of UTF8 strings should happen before this
 * function is called. */
int
nc4_check_dup_name(NC_GRP_INFO_T *grp, char *name)
{
   NC_TYPE_INFO_T *type;
   NC_GRP_INFO_T *g;
   NC_VAR_INFO_T *var;
   uint32_t hash;
   int i;

   /* Any types of this name? */
   type = NC_listmap_get(&grp->type,name);
   if(type != NULL)
	 return NC_ENAMEINUSE;

   /* Any child groups of this name? */
   g = NC_listmap_get(&grp->children,name);
   if(g != NULL)
	 return NC_ENAMEINUSE;

   /* Any variables of this name? */
   var = NC_listmap_get(&grp->vars.value,name);
   if(var != NULL)
	 return NC_ENAMEINUSE;
   return NC_NOERR;
}

/* Return a pointer to the new var. */
int
nc4_var_new(const char* name, int ndims, NC_VAR_INFO_T **var)
{
   NC_VAR_INFO_T *new_var;
   int i;

   /* Allocate storage for new variable. */
   if (!(new_var = calloc(1, sizeof(NC_VAR_INFO_T))))
      return NC_ENOMEM;

   /* These are the HDF5-1.8.4 defaults. */
   new_var->chunk_cache_size = nc4_chunk_cache_size;
   new_var->chunk_cache_nelems = nc4_chunk_cache_nelems;
   new_var->chunk_cache_preemption = nc4_chunk_cache_preemption;

   /* Init other fields */
   new_var->name = nulldup(name);
   new_var->ndims = ndims;

   if(!NC_listmap_init(&new_var->att,0))
	return NC_ENOMEM;

   /* Set the var pointer, assume given */
   *var = new_var;
   return NC_NOERR;
}

/* Delete a var, and free the memory. */
int
nc4_var_free(NC_VAR_INFO_T *var)
{
   NC_ATT_INFO_T *att;
   int ret;
   size_t iter;

   if(var == NULL)
     return NC_NOERR;

   /* First delete all the attributes attached to this var. */
   for(iter=0;NC_listmap_next(&var->att,iter,(uintptr_t*)&att);iter++)
   {
      if ((ret = nc4_att_free(att)))
	 return ret;
   }
   NC_listmap_clear(&var->att);

   /* Free some things that may be allocated. */
   if (var->chunksizes)
     {free(var->chunksizes);var->chunksizes = NULL;}

   if (var->hdf5_name)
     {free(var->hdf5_name); var->hdf5_name = NULL;}

   if (var->name)
     {free(var->name); var->name = NULL;}

   if (var->dimids)
     {free(var->dimids); var->dimids = NULL;}

   if (var->dim)
     {free(var->dim); var->dim = NULL;}

   /* Delete any fill value allocation. This must be done before the
    * type_info is freed. */
   if (var->fill_value)
   {
      if (var->hdf_datasetid)
      {
         if (var->type_info)
         {
            if (var->type_info->nc_type_class == NC_VLEN)
               nc_free_vlen((nc_vlen_t *)var->fill_value);
            else if (var->type_info->nc_type_class == NC_STRING && *(char **)var->fill_value)
               free(*(char **)var->fill_value);
         }
      }
      free(var->fill_value);
      var->fill_value = NULL;
   }

   /* Release type information */
   if (var->type_info)
   {
      int retval;

      if ((retval = nc4_type_free(var->type_info)))
          return retval;
      var->type_info = NULL;
   }

   /* Delete any HDF5 dimscale objid information. */
   if (var->dimscale_hdf5_objids)
      free(var->dimscale_hdf5_objids);

   /* Delete information about the attachment status of dimscales. */
   if (var->dimscale_attached)
      free(var->dimscale_attached);

   NC_listmap_clear(&var->att);

   /* Delete the var. */
   free(var);

   return NC_NOERR;
}

/* See also nc4var.c#nc4_vararray_add */

/* Create and partially initialize new dim object */
int
nc4_dim_new(const char* name, NC_DIM_INFO_T **dim)
{
   NC_DIM_INFO_T *new_dim;

   if (!(new_dim = calloc(1, sizeof(NC_DIM_INFO_T))))
      return NC_ENOMEM;

   new_dim->name = nulldup(name);
   new_dim->dimid = 0; /* let someone else assign this */

   /* Set the dim pointer */
   *dim = new_dim;

   return NC_NOERR;
}

/* Delete a dim and nc_free the memory. */
int
nc4_dim_free(NC_DIM_INFO_T *dim)
{
   /* Free memory allocated for names. */
   if (dim->name)
      free(dim->name);
   free(dim);
   return NC_NOERR;
}

/* Add to the beginning of a dim list. */
int
nc4_dim_list_add(NC_GRP_INFO_T* parent, NC_DIM_INFO_T *new_dim)
{
   NC_HDF5_FILE_INFO_T* h5 = parent->nc4_info;

   new_dim->dimid = nclistlength(h5->alldims);

   /* Add object to lists */
   nclistpush(h5->alldims, new_dim);
   NC_listmap_add(&parent->dim, new_dim);

   return NC_NOERR;
}

/* Create an instance of NC_ATT_INFO_T; value not set*/
int
nc4_att_new(const char* name, NC_ATT_INFO_T **attp)
{
   NC_ATT_INFO_T *new_att;

   if (!(new_att = calloc(1, sizeof(NC_ATT_INFO_T))))
      return NC_ENOMEM;

   if (!(new_att->name = strdup(name)))
   {
      free(new_att);
      return NC_ENOMEM;
   }
   *attp = new_att;
   return NC_NOERR;
}

/* Free an instance of NC_ATT_INFO_T */
int
nc4_att_free(NC_ATT_INFO_T *att)
{
   int i;

   /* Free memory that was malloced to hold data for this
    * attribute. */
   if (att->data)
      free(att->data);

   /* Free the name. */
   if (att->name)
      free(att->name);

   /* Close the HDF5 typeid. */
   if (att->native_hdf_typeid && H5Tclose(att->native_hdf_typeid) < 0)
      return NC_EHDFERR;

   /* If this is a string array attribute, delete all members of the
    * string array, then delete the array of pointers to strings. (The
    * array was filled with pointers by HDF5 when the att was read,
    * and memory for each string was allocated by HDF5. That's why I
    * use free and not nc_free, because the netCDF library didn't
    * allocate the memory that is being freed.) */
   if (att->stdata)
   {
      for (i = 0; i < att->len; i++)
         if(att->stdata[i])
	    free(att->stdata[i]);
      free(att->stdata);
   }

   /* If this att has vlen data, release it. */
   if (att->vldata)
   {
      for (i = 0; i < att->len; i++)
	 nc_free_vlen(&att->vldata[i]);
      free(att->vldata);
   }

   free(att);
   return NC_NOERR;
}

/* Add to the end of an att list. */
int
nc4_att_list_add(NC_listmap* list, NC_ATT_INFO_T *new_att)
{
   new_att->attnum = NC_listmap_size(list);

   /* Add object to list */
   NC_listmap_add(list, new_att);

   return NC_NOERR;
}

int
nc4_grp_new(NC_GRP_INFO_T* parent_grp, char* name, NC_GRP_INFO_T** grpp)
{
   NC_GRP_INFO_T *new_grp;

   /* Get the memory to store this groups info. */
   if (!(new_grp = calloc(1, sizeof(NC_GRP_INFO_T))))
      return NC_ENOMEM;

   new_grp->parent = parent_grp;
   if (!(new_grp->name = strdup(name)))
   {
      free(new_grp);
      return NC_ENOMEM;
   }
   if(!NC_listmap_init(&new_grp->vars.value,0)) return NC_ENOMEM;
   if(!NC_listmap_init(&new_grp->children,0)) return NC_ENOMEM;
   if(!NC_listmap_init(&new_grp->dim,0)) return NC_ENOMEM;
   if(!NC_listmap_init(&new_grp->att,0)) return NC_ENOMEM;
   if(!NC_listmap_init(&new_grp->type,0)) return NC_ENOMEM;
   *grpp = new_grp;
   return NC_NOERR;   
}

/* Recursively delete the data for a group (and everything it
 * contains) in our internal metadata store. */
int
nc4_rec_grp_del(NC_GRP_INFO_T *grp)
{
   NC_GRP_INFO_T *g, *c;
   NC_VAR_INFO_T *var;
   NC_ATT_INFO_T *a, *att;
   NC_DIM_INFO_T *d, *dim;
   NC_TYPE_INFO_T *type, *t;
   int retval;
   int i;
   size_t iter;

   assert(grp);
   LOG((3, "%s: grp->name %s", __func__, grp->name));

   /* Recursively call this function for each child, if any, stopping
    * if there is an error. */
   for(iter = 0;NC_listmap_next(&grp->children,iter,(uintptr_t*)&g);iter++) {
      if ((retval = nc4_rec_grp_del(g)))
	 return retval;
   }
   NC_listmap_clear(&grp->children);

   /* Delete all the list contents for vars, dims, and atts, in each
    * group. */

   for(iter=0;NC_listmap_next(&grp->att,iter,(uintptr_t*)&att);iter++) {
      LOG((4, "%s: deleting att %s", __func__, att->name));
      if ((retval = nc4_att_free(att)))
	 return retval;
   }

   /* Delete all vars. */
   for(iter=0;NC_listmap_next(&grp->vars.value,iter,(uintptr_t*)&var);iter++) {
      if(var == NULL) continue;
      LOG((4, "%s: deleting var %s", __func__, var->name));
      /* Close HDF5 dataset associated with this var, unless it's a
       * scale. */
      if (var->hdf_datasetid && H5Dclose(var->hdf_datasetid) < 0)
	 return NC_EHDFERR;
      if ((retval = nc4_var_free(var)))
	 return retval;
   }

   /* Vars are all freed above. Free the containing listmap */
   NC_listmap_clear(&grp->vars.value);

   /* Delete all dims. */
   for(iter=0;NC_listmap_next(&grp->dim,iter,(uintptr_t*)&dim);iter++) {
      if(dim == NULL) continue;
      LOG((4, "%s: deleting dim %s", __func__, dim->name));
      /* Close HDF5 dataset associated with this dim. */
      if (dim->hdf_dimscaleid && H5Dclose(dim->hdf_dimscaleid) < 0)
	 return NC_EHDFERR;
      if ((retval = nc4_dim_free(dim)))
	 return retval;
   }
   NC_listmap_clear(&grp->dim);

   /* Delete all types. */
   for(iter=0;NC_listmap_next(&grp->type,iter,(uintptr_t*)&type);iter++) {
      if(type == NULL) continue;
      LOG((4, "%s: deleting type %s", __func__, type->name));
      if ((retval = nc4_type_free(type)))
	 return retval;
   }
   NC_listmap_clear(&grp->type);

   /* Tell HDF5 we're closing this group. */
   LOG((4, "%s: closing group %s", __func__, grp->name));
   if (grp->hdf_grpid && H5Gclose(grp->hdf_grpid) < 0)
      return NC_EHDFERR;

   /* Free the name. */
   free(grp->name);

   return NC_NOERR;
}

/* Add to the end of a group list. Can't use 0 as a new_nc_grpid -
 * it's reserverd for the root group. */
int
nc4_grp_list_add(NC_HDF5_FILE_INFO_T* h5, NC_GRP_INFO_T *new_grp)
{
   int new_nc_grpid = nclistlength(h5->allgroups);
   LOG((3, "%s: new_nc_grpid %d name %s ", __func__, new_nc_grpid, new_grp->name));
   NC_GRP_INFO_T* parent_grp = new_grp->parent;

   /* Fill in this group's information. */
   new_grp->nc_grpid = new_nc_grpid;
   new_grp->nc4_info = h5;

   /* Add object to allgroups list and parent list*/
   nclistpush(h5->allgroups, new_grp);
   /* If grp is NULL, then we are creating the root group so no parent */
   if(parent_grp != NULL)
     NC_listmap_add(&parent_grp->children, new_grp);

   return NC_NOERR;
}

int
nc4_type_new(nc_type typeclass, size_t size, const char *name, NC_TYPE_INFO_T **typep)
{
   NC_TYPE_INFO_T *new_type;
   /* Allocate memory for the type */
   if (!(new_type = calloc(1, sizeof(NC_TYPE_INFO_T))))
      return NC_ENOMEM;

   /* Remember info about this type. */
   new_type->nc_type_class = typeclass;
   new_type->size = size;
   if (!(new_type->name = strdup(name)))
      return NC_ENOMEM;
   switch (typeclass) {
   case NC_ENUM: if((new_type->u.e.members = nclistnew()) == NULL) return NC_ENOMEM;
   case NC_COMPOUND: if((new_type->u.c.fields = nclistnew()) == NULL) return NC_ENOMEM;
   default: break;
   }

   *typep = new_type;
   return NC_NOERR;
}

/* Add to the end of a type list. */
int
nc4_type_list_add(NC_GRP_INFO_T* grp, NC_TYPE_INFO_T *new_type)
{
   NC_listmap* list;
   NC_HDF5_FILE_INFO_T* h5 = grp->nc4_info;

   new_type->nc_typeid = nclistlength(h5->alltypes); 

   /* Add object to lists */
   nclistpush(h5->alltypes,new_type);
   NC_listmap_add(&grp->type, new_type);

   /* Increment the ref. count on the type */
   new_type->rc++;

   return NC_NOERR;
}

/* Create */
int
nc4_field_new(const char *name,
		   size_t offset, hid_t field_hdf_typeid, hid_t native_typeid,
		   nc_type xtype, int ndims, const int *dim_sizesp, NC_FIELD_INFO_T** fieldp)
{
   NC_FIELD_INFO_T *field;

   /* Name has already been checked and UTF8 normalized. */
   if (!name)
      return NC_EINVAL;

   /* Allocate storage for this field information. */
   if (!(field = calloc(1, sizeof(NC_FIELD_INFO_T))))
      return NC_ENOMEM;

   /* Store the information about this field. */
   if (!(field->name = strdup(name)))
   {
      free(field);
      return NC_ENOMEM;
   }
   field->hdf_typeid = field_hdf_typeid;
   field->native_hdf_typeid = native_typeid;
   field->nc_typeid = xtype;
   field->offset = offset;
   field->ndims = ndims;
   if (ndims)
   {
      int i;

      if (!(field->dim_size = malloc(ndims * sizeof(int))))
      {
         free(field->name);
         free(field);
	 return NC_ENOMEM;
      }
      for (i = 0; i < ndims; i++)
	 field->dim_size[i] = dim_sizesp[i];
   }
   *fieldp = field;
   return NC_NOERR;
}

/* Delete a field from a field list, and nc_free the memory. */
int
nc4_field_free(NC_FIELD_INFO_T *field)
{
   /* Free some stuff. */
   if (field->name)
      free(field->name);
   if (field->dim_size)
      free(field->dim_size);
   /* free the memory. */
   free(field);
   return NC_NOERR;
}

int
nc4_field_list_add(NC_TYPE_INFO_T* parent, NC_FIELD_INFO_T* field)
{
   /* Add object to list */
   if(parent->u.c.fields == NULL
	&& (parent->u.c.fields = nclistnew()) == NULL) return NC_ENOMEM;
   field->fieldid = nclistlength(parent->u.c.fields);
   nclistpush(parent->u.c.fields,field);
   return NC_NOERR;
}

/* Create Instance of NC_ENUM_MEMBER_INFO_T */
int
nc4_enum_member_new(size_t size, const char *name, const void *value, NC_ENUM_MEMBER_INFO_T** emp)
{
   NC_ENUM_MEMBER_INFO_T *member;

   /* Name has already been checked. */
   assert(name && size > 0 && value);
   LOG((4, "%s: size %d name %s", __func__, size, name));

   /* Allocate storage for this field information. */
   if (!(member = calloc(1, sizeof(NC_ENUM_MEMBER_INFO_T))))
      return NC_ENOMEM;
   if (!(member->value = malloc(size))) {
      free(member);
      return NC_ENOMEM;
   }
   if (!(member->name = strdup(name))) {
      free(member->value);
      free(member);
      return NC_ENOMEM;
   }

   /* Store the value for this member. */
   memcpy(member->value, value, size);

   *emp = member;

   return NC_NOERR;
}

/* Free Instance of NC_ENUM_MEMBER_INFO_T */
int
nc4_enum_member_free(NC_ENUM_MEMBER_INFO_T *member)
{
    if(member) {
        if(member->name) free(member->name);
        if(member->value) free(member->value);
	free(member);
    }
    return NC_NOERR;
}

/* Add a member to an enum type. */
int
nc4_member_list_add(NC_TYPE_INFO_T* parent, NC_ENUM_MEMBER_INFO_T *member)
{
   /* Add object to list */
   if(parent->u.e.members == NULL
	&& (parent->u.e.members = nclistnew()) == NULL) return NC_ENOMEM;
   nclistpush(parent->u.e.members, member);
   return NC_NOERR;
}

/* Free allocated space for type information. */
int
nc4_type_free(NC_TYPE_INFO_T *type)
{
   int i;

   /* Decrement the ref. count on the type */
   assert(type->rc);
   type->rc--;

   /* Release the type, if the ref. count drops to zero */
   if (0 == type->rc)
   {
      /* Close any open user-defined HDF5 typeids. */
      if (type->hdf_typeid && H5Tclose(type->hdf_typeid) < 0)
         return NC_EHDFERR;
      if (type->native_hdf_typeid && H5Tclose(type->native_hdf_typeid) < 0)
         return NC_EHDFERR;

      /* Free the name. */
      if (type->name)
         free(type->name);

      /* Class-specific cleanup */
      switch (type->nc_type_class)
      {
         case NC_COMPOUND:
            {
               /* Delete all the fields in this type (there will be some if its a compound). */
	       for(i=0;i<nclistlength(type->u.c.fields);i++)
                  nc4_field_free(nclistget(type->u.c.fields,i));
	       nclistfree(type->u.c.fields);
            }
            break;

         case NC_ENUM:
            {
               NC_ENUM_MEMBER_INFO_T *enum_member = NULL;
               /* Delete all the enum_members, if any. */
	       for(i=0;i<nclistlength(type->u.e.members);i++)
               {
		  enum_member = nclistget(type->u.e.members,i);
                  free(enum_member->value);
                  free(enum_member->name);
                  free(enum_member);
               }
	       nclistfree(type->u.e.members);
               if (H5Tclose(type->u.e.base_hdf_typeid) < 0)
                  return NC_EHDFERR;
            }
            break;

         case NC_VLEN:
            if (H5Tclose(type->u.v.base_hdf_typeid) < 0)
               return NC_EHDFERR;

         default:
            break;
      }

      /* Release the memory. */
      free(type);
   }

   return NC_NOERR;
}


/* remove an attribute from a list and renumber the following elements */
int
nc4_att_list_del(NC_listmap* list, NC_ATT_INFO_T *att)
{
    int oldattnum = 0;
    size_t pos = 0;
    NC_ATT_INFO_T *tmp;

    if(list == NULL || NC_listmap_size(list) == 0 || att == NULL)
	return NC_ENOTATT;    
    oldattnum = att->attnum; /* save for later */
    assert(oldattnum <= NC_listmap_size(list));
    /* Verify the att number */
    tmp = NC_listmap_iget(list,oldattnum);
    if(tmp == NULL || tmp->attnum != oldattnum)
	return NC_ENOTATT; /* mismatch */
    if(!NC_listmap_idel(list,oldattnum))
	return NC_ENOTATT;
    /* Renumber */
    for(pos=oldattnum;pos<NC_listmap_size(list);pos++) {
        tmp = NC_listmap_iget(list,pos);
        tmp->attnum--;
	if(!NC_listmap_setdata(list,tmp,tmp->attnum))
	    return NC_EINTERNAL;
    }
    /* Now free the deleted attribute */
    nc4_att_free(att);
    return NC_NOERR;        
}


/* Break a coordinate variable to separate the dimension and the variable */
int
nc4_break_coord_var(NC_GRP_INFO_T *grp, NC_VAR_INFO_T *coord_var, NC_DIM_INFO_T *dim)
{
   int retval = NC_NOERR;

   /* Sanity checks */
   assert(dim->coord_var == coord_var);
   assert(coord_var->dim[0] == dim);
   assert(coord_var->dimids[0] == dim->dimid);
   assert(0 == dim->hdf_dimscaleid);

   /* If we're replacing an existing dimscale dataset, go to
    * every var in the file and detach this dimension scale. */
   if ((retval = rec_detach_scales(grp->nc4_info->root_grp,
                                   dim->dimid, coord_var->hdf_datasetid)))
      return retval;

   /* Allow attached dimscales to be tracked on the [former] coordinate variable */
   if (coord_var->ndims)
   {
      /* Coordinate variables shouldn't have dimscales attached */
      assert(NULL == coord_var->dimscale_attached);

      /* Allocate space for tracking them */
      if (NULL == (coord_var->dimscale_attached = calloc(coord_var->ndims, sizeof(nc_bool_t))))
         return NC_ENOMEM;
   }

   /* Detach dimension from variable */
   coord_var->dimscale = NC_FALSE;
   dim->coord_var = NULL;

   /* Set state transition indicators */
   coord_var->was_coord_var = NC_TRUE;
   coord_var->became_coord_var = NC_FALSE;

   return NC_NOERR;
}

/* Reform a coordinate variable from a dimension and a variable */
int
nc4_reform_coord_var(NC_GRP_INFO_T *grp, NC_VAR_INFO_T *var, NC_DIM_INFO_T *dim)
{
   int retval = NC_NOERR;

   /* Detach dimscales from the [new] coordinate variable */
   if(var->dimscale_attached)
   {
      int dims_detached = 0;
      int finished = 0;
      int d;

      /* Loop over all dimensions for variable */
      for (d = 0; d < var->ndims && !finished; d++)
         /* Is there a dimscale attached to this axis? */
         if(var->dimscale_attached[d])
         {
            NC_GRP_INFO_T *g;

            for (g = grp; g && !finished; g = g->parent)
            {
               NC_DIM_INFO_T *dim1;
	       size_t diter;

	       for(diter=0;!finished && NC_listmap_next(&g->dim,diter,(uintptr_t*)&dim1);diter++) {
                  if (var->dimids[d] == dim1->dimid)
                  {
                     hid_t dim_datasetid;  /* Dataset ID for dimension */

                     /* Find dataset ID for dimension */
                     if (dim1->coord_var)
                         dim_datasetid = dim1->coord_var->hdf_datasetid;
                     else
                         dim_datasetid = dim1->hdf_dimscaleid;
                     assert(dim_datasetid > 0);
                     if (H5DSdetach_scale(var->hdf_datasetid, dim_datasetid, d) < 0)
                        BAIL(NC_EHDFERR);
                     var->dimscale_attached[d] = NC_FALSE;
                     if (dims_detached++ == var->ndims)
                        finished++;
                  }
		}
            }
         }

      /* Release & reset the array tracking attached dimscales */
      free(var->dimscale_attached);
      var->dimscale_attached = NULL;
   }

   /* Use variable's dataset ID for the dimscale ID */
   if (dim->hdf_dimscaleid && grp != NULL)
   {
      if (H5Dclose(dim->hdf_dimscaleid) < 0)
         BAIL(NC_EHDFERR);
      dim->hdf_dimscaleid = 0;

      /* Now delete the dimscale's dataset
         (it will be recreated later, if necessary) */
      if (H5Gunlink(grp->hdf_grpid, dim->name) < 0)
        return NC_EDIMMETA;
   }

   /* Attach variable to dimension */
   var->dimscale = NC_TRUE;
   dim->coord_var = var;

   /* Check if this variable used to be a coord. var */
   if (var->was_coord_var && grp != NULL)
   {
      /* Reattach the scale everywhere it is used. */
      /* (Recall that netCDF dimscales are always 1-D) */
      if ((retval = rec_reattach_scales(grp->nc4_info->root_grp,
                                        var->dimids[0], var->hdf_datasetid)))
         return retval;

      /* Set state transition indicator (cancels earlier transition) */
      var->was_coord_var = NC_FALSE;
   }
   else
      /* Set state transition indicator */
      var->became_coord_var = NC_TRUE;

  exit:
   return retval;
}

/* Normalize a UTF8 name. Put the result in norm_name, which can be
 * NC_MAX_NAME + 1 in size. This function makes sure the free() gets
 * called on the return from utf8proc_NFC, and also ensures that the
 * name is not too long. */
int
nc4_normalize_name(const char *name, char *norm_name)
{
   char *temp_name;
   int stat = nc_utf8_normalize((const unsigned char *)name,(unsigned char **)&temp_name);
   if(stat != NC_NOERR)
      return stat;
   if (strlen(temp_name) > NC_MAX_NAME)
   {
      free(temp_name);
      return NC_EMAXNAME;
   }
   strcpy(norm_name, temp_name);
   free(temp_name);
   return NC_NOERR;
}

/* Print out a bunch of info to stderr about the metadata for
   debugging purposes. */
#ifdef LOGGING
/* Use this to set the global log level. Set it to NC_TURN_OFF_LOGGING
   (-1) to turn off all logging. Set it to 0 to show only errors, and
   to higher numbers to show more and more logging details. */
int
nc_set_log_level(int new_level)
{
   if(!nc4_hdf5_initialized)
	nc4_hdf5_initialize();

   /* If the user wants to completely turn off logging, turn off HDF5
      logging too. Now I truely can't think of what to do if this
      fails, so just ignore the return code. */
   if (new_level == NC_TURN_OFF_LOGGING)
   {
      set_auto(NULL,NULL);
      LOG((1, "HDF5 error messages turned off!"));
   }

   /* Do we need to turn HDF5 logging back on? */
   if (new_level > NC_TURN_OFF_LOGGING &&
       nc_log_level <= NC_TURN_OFF_LOGGING)
   {
      if (set_auto((H5E_auto_t)&H5Eprint, stderr) < 0)
	 LOG((0, "H5Eset_auto failed!"));
      LOG((1, "HDF5 error messages turned on."));
   }

   /* Now remember the new level. */
   nc_log_level = new_level;
   LOG((4, "log_level changed to %d", nc_log_level));
   return 0;
}

/* Recursively print the metadata of a group. */
#define MAX_NESTS 10
static int
rec_print_metadata(NC_GRP_INFO_T *grp, int tab_count)
{
   NC_GRP_INFO_T *g;
   NC_ATT_INFO_T *att;
   NC_VAR_INFO_T *var;
   NC_DIM_INFO_T *dim;
   NC_TYPE_INFO_T *type;
   NC_FIELD_INFO_T *field;
   char tabs[MAX_NESTS] = "";
   char *dims_string = NULL;
   char temp_string[10];
   int t, retval, d, i;
   size_t iter;

   /* Come up with a number of tabs relative to the group. */
   for (t = 0; t < tab_count && t < MAX_NESTS; t++)
      strcat(tabs, "\t");

   LOG((2, "%s GROUP - %s nc_grpid: %d nvars: %d natts: %d",
	tabs, grp->name, grp->nc_grpid, NC_listmap_size(&grp->vars.value), NC_listmap_size(&grp->att)));

   for(iter=0;NC_listmap_next(&grp->att,iter,(uintptr_t*)&att);iter++)
      LOG((2, "%s GROUP ATTRIBUTE - attnum: %d name: %s type: %d len: %d",
	   tabs, att->attnum, att->name, att->nc_typeid, att->len));

   for(iter=0;NC_listmap_next(&grp->dim,iter,(uintptr_t*)&dim);iter++)
      LOG((2, "%s DIMENSION - dimid: %d name: %s len: %d unlimited: %d",
	   tabs, dim->dimid, dim->name, dim->len, dim->unlimited));

   for(iter=0;NC_listmap_next(&grp->vars.value,iter,(uintptr_t*)&var);iter++)
   {
      size_t aiter;
      if (!var) continue;
      if(var->ndims > 0)
      {
         dims_string = (char*)malloc(sizeof(char)*(var->ndims*4));
         strcpy(dims_string, "");
         for (d = 0; d < var->ndims; d++)
           {
             sprintf(temp_string, " %d", var->dimids[d]);
             strcat(dims_string, temp_string);
           }
      }
      LOG((2, "%s VARIABLE - varid: %d name: %s type: %d ndims: %d dimscale: %d dimids:%s endianness: %d, hdf_typeid: %d",
	   tabs, var->varid, var->name, var->type_info->nc_typeid, var->ndims, (int)var->dimscale,
       (dims_string ? dims_string : " -"),var->type_info->endianness, var->type_info->native_hdf_typeid));
      for(aiter=0;NC_listmap_next(&var->att,aiter,(uintptr_t*)&att);aiter++) {
	 LOG((2, "%s VAR ATTRIBUTE - attnum: %d name: %s type: %d len: %d",
	      tabs, att->attnum, att->name, att->nc_typeid, att->len));
      }
      if(dims_string)
      {
         free(dims_string);
         dims_string = NULL;
      }
   }

   for(iter=0;NC_listmap_next(&grp->type,iter,(uintptr_t*)&type);iter++) {
      LOG((2, "%s TYPE - nc_typeid: %d hdf_typeid: 0x%x size: %d committed: %d "
	   "name: %s", tabs, type->nc_typeid,
	   type->hdf_typeid, type->size, (int)type->committed, type->name));
      /* Is this a compound type? */
      if (type->nc_type_class == NC_COMPOUND)
      {
	 int i;
	 LOG((3, "compound type: num_fields: %d", nclistlength(type->u.c.fields)));
	 for (i=0;i<nclistlength(type->u.c.fields);i++) {
	    field = nclistget(type->u.c.fields,i);
	    LOG((4, "field %s offset %d nctype %d ndims %d", field->name,
		 field->offset, field->nc_typeid, field->ndims));
	 }
      }
      else if (type->nc_type_class == NC_VLEN)
      {
	 LOG((3, "VLEN type"));
         LOG((4, "base_nc_type: %d", type->u.v.base_nc_typeid));
      }
      else if (type->nc_type_class == NC_OPAQUE)
	 LOG((3, "Opaque type"));
      else if (type->nc_type_class == NC_ENUM)
      {
	 LOG((3, "Enum type: num_members: %d", nclistlength(type->u.e.members)));
         LOG((4, "base_nc_type: %d", type->u.e.base_nc_typeid));
      }
      else
      {
	 LOG((0, "Unknown class: %d", type->nc_type_class));
	 return NC_EBADTYPE;
      }
   }

   /* Call self for each child of this group. */
   if (NC_listmap_size(&grp->children) > 0)
   {
      size_t giter;
      for(giter=0;NC_listmap_next(&grp->children,giter,(uintptr_t*)&g);giter++) {
	 if ((retval = rec_print_metadata(g, tab_count + 1)))
	    return retval;
      }
   }

   return NC_NOERR;
}

/* Print out the internal metadata for a file. This is useful to check
 * that netCDF is working! Nonetheless, this function will print
 * nothing if logging is not set to at least two. */
int
log_metadata_nc(NC *nc)
{
   NC_HDF5_FILE_INFO_T *h5 = NC4_DATA(nc);

   LOG((2, "\n*** NetCDF-4 Internal Metadata: int_ncid 0x%x ext_ncid 0x%x",
	nc->int_ncid, nc->ext_ncid));
   if (!h5)
   {
      LOG((2, "This is a netCDF-3 file."));
      return NC_NOERR;
   }
   LOG((2, "FILE - hdfid: 0x%x path: %s cmode: 0x%x parallel: %d redef: %d "
	"fill_mode: %d no_write: %d",	h5->hdfid, nc->path,
	h5->cmode, (int)h5->parallel, (int)h5->redef, h5->fill_mode, (int)h5->no_write));
   return rec_print_metadata(h5->root_grp, 0);
}

#endif /*LOGGING */

/* Show the in-memory metadata for a netcdf file. */
int
NC4_show_metadata(int ncid)
{
   int retval = NC_NOERR;
#ifdef LOGGING
   NC *nc;
   int old_log_level = nc_log_level;

   /* Find file metadata. */
   if (!(nc = nc4_find_nc_file(ncid,NULL)))
      return NC_EBADID;

   /* Log level must be 2 to see metadata. */
   nc_log_level = 2;
   retval = log_metadata_nc(nc);
   nc_log_level = old_log_level;
#endif /*LOGGING*/
   return retval;
}
