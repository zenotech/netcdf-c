/*
Copyright (c) 1998-2017 University Corporation for Atmospheric Research/Unidata
See LICENSE.txt for license information.
*/

/** \file \internal
Internal netcdf-4 functions.

This file contains functions for manipulating NC_listmap
objects.
*/

#include "config.h"
#include <stdlib.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <assert.h>
#include "nclistmap.h"

/* Keep the table sizes small initially */
#define DFALTTABLESIZE 7

/* Locate object by name in an NC_LISTMAP */
void*
NC_listmap_get(NC_listmap* listmap, const char* name)
{
   uintptr_t index;
   void* obj;
   if(listmap == NULL || name == NULL)
	return NULL;
   assert(listmap->map != NULL);
   if(!NC_hashmapget(listmap->map,name,&index))
	return NULL; /* not present */
   obj = nclistget(listmap->list,(size_t)index);
   return obj;
}

/* Locate object by index in an NC_LISTMAP */
void*
NC_listmap_iget(NC_listmap* listmap, size_t index)
{
   if(listmap == NULL)
	return NULL;
   assert(listmap->list != NULL);
   return nclistget(listmap->list,index);
}

/* Get the index of an object; if not present, return 0
   (=> you have to do your own presence check to avoid ambiguity)
*/
uintptr_t
NC_listmap_index(NC_listmap* listmap, void* obj)
{
   uintptr_t index;
   const char* name;
   if(listmap == NULL || obj == NULL)
	return 0;
   assert(listmap->map != NULL);
   name = *((const char**)obj);
   if(!NC_hashmapget(listmap->map,name,&index))
	return 0; /* not present */
   return index;
}

/* Add object to the end of an index; assume cast (char**)obj is defined */
/* Return 1 if ok, 0 otherwise.*/
int
NC_listmap_add(NC_listmap* listmap, void* obj)
{
   size_t index = nclistlength(listmap->list);
   if(listmap == NULL) return 0;
   if(!nclistpush(listmap->list,obj)) return 0;
   NC_hashmapadd(listmap->map,(uintptr_t)index,*(char**)obj);
   return 1;
}

/* Add object at specific index; will overwrite anything already there;
   obj == NULL is ok.
   Return 1 if ok, 0 otherwise.
 */
int
NC_listmap_iput(NC_listmap* listmap, size_t pos, void* obj)
{
    if(listmap == NULL) return 0;
    if(obj != NULL) {
	uintptr_t index = 0;
	const char* name = *(const char**)obj;
	if(name != NULL) return 0;
	if(pos >= nclistlength(listmap->list)) return 0;
	/* Temporarily remove from hashmap */
	if(!NC_hashmapremove(listmap->map,name,&index))
            return 0; /* not there */
	/* Reinsert with new pos */
	NC_hashmapadd(listmap->map,(uintptr_t)pos,*(char**)obj);
    }
    /* Insert at pos into listmap vector */
    nclistset(listmap->list,pos,obj);
    return 1;
}

/* Remove object from listmap; assume cast (char**)target is defined */
/* Return 1 if ok, 0 otherwise.*/
int
NC_listmap_del(NC_listmap* listmap, void* target)
{
   uintptr_t index;
   void* obj;
   if(listmap == NULL || target == NULL) return 0;
   if(!NC_hashmapget(listmap->map,*(char**)target,&index))
	return 0; /* not present */
   obj = nclistremove(listmap->list,(size_t)index);
   if(obj != NULL)
       assert(obj == target);
   return 1;
}

/* Remove object from listmap by index */
/* Return 1 if ok, 0 otherwise.*/
int
NC_listmap_idel(NC_listmap* listmap, size_t index)
{
   void* obj;
   if(listmap == NULL) return 0;
   obj = nclistget(listmap->list,index);
   if(obj == NULL)
	return 0; /* not present */
   if(!NC_hashmapremove(listmap->map,*(char**)obj,NULL))
	return 0; /* not present */
   obj = nclistremove(listmap->list,index);
   return 1;
}

/* Pseudo iterator; start index at 0, return 0 when complete.
   Usage:
      size_t iter;
      uintptr_t data      
      for(iter=0;NC_listmap_next(listmap,iter,(uintptr_t*)&data);iter++) {f(data);}
*/
size_t
NC_listmap_next(NC_listmap* listmap, size_t index, uintptr_t* datap)
{
    size_t len = nclistlength(listmap->list);
    if(datap) *datap = 0;
    if(len == 0) return 0;
    if(index >= nclistlength(listmap->list)) return 0;
    if(datap) *datap = (uintptr_t)nclistget(listmap->list,index);
    return index+1;
}

/* Reverse pseudo iterator; start index at 0, return 1 if more data, 0 if done.
   Differs from NC_listmap_next in that it iterates from last to first.
   This means that the iter value cannot be directly used as an index
   for e.g. NC_listmap_iget().
   Usage:
      size_t iter;
      uintptr_t data;
      for(iter=0;NC_listmap_next(listmap,iter,(uintptr_t*)&data);iter++) {f(data);}
*/
size_t
NC_listmap_prev(NC_listmap* listmap, size_t iter, uintptr_t* datap)
{
    size_t len = nclistlength(listmap->list);
    size_t index;
    if(datap) *datap = 0;
    if(len == 0) return 0;
    if(iter >= len) return 0;
    index = (len - iter) - 1;
    if(datap) *datap = (uintptr_t)nclistget(listmap->list,index);
    return iter+1;
}

/* Rehash object with new name */
/* Return 1 if ok, 0 otherwise.*/
int
NC_listmap_move(NC_listmap* listmap, uintptr_t obj)
{ 
   const char* new_name = *(const char**)obj;
   return NC_hashmapmove(listmap->map,(uintptr_t)obj,new_name);
}

/* Clear a list map without free'ing the map itself */
int
NC_listmap_clear(NC_listmap* listmap)
{
    nclistfree(listmap->list);
    NC_hashmapfree(listmap->map);
    listmap->list = NULL;    
    listmap->map = NULL;    
    return 1;
}

/* Initialize a list map without malloc'ing the map itself */
int
NC_listmap_init(NC_listmap* listmap, size_t size0)
{
    size_t size = (size0 == 0 ? DFALTTABLESIZE : size0);
    listmap->list = nclistnew();
    if(listmap->list != NULL)
	nclistsetalloc(listmap->list,size);
    listmap->map = NC_hashmapnew(size);
    return 1;
}


