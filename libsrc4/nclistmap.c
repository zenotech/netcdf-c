/*
Copyright (c) 1998-2017 University Corporation for Atmospheric Research/Unidata
See LICENSE.txt for license information.
*/

/** \file \internal
Internal netcdf-4 functions.

This file contains functions for manipulating NC_listmap
objects.

Warning: This code depends critically on the assumption that
|void*| == |uintptr_t|

*/

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
   if(!NC_hashmapget(listmap->map,name,(void**)&index))
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
size_t
NC_listmap_index(NC_listmap* listmap, void* obj)
{
   uintptr_t* index;
   const char* name;
   if(listmap == NULL || obj == NULL)
	return 0;
   assert(listmap->map != NULL);
   name = *((const char**)obj);
   if(!NC_hashmapget(listmap->map,name,(void**)&index))
	return 0; /* not present */
   return (size_t)index;
}

/* Add object to the end of an index; assume cast (char**)obj is defined */
/* Return 1 if ok, 0 otherwise.*/
int
NC_listmap_add(NC_listmap* listmap, void* obj)
{
   uintptr_t index = (uintptr_t)nclistlength(listmap->list);
   if(listmap == NULL) return 0;
   if(!nclistpush(listmap->list,obj)) return 0;
   NC_hashmapadd(listmap->map,(void*)index,*(char**)obj);
   return 1;
}

#if 0
/* Add object at specific index; will overwrite anything already there;
   obj == NULL is ok.
   Return 1 if ok, 0 otherwise.
 */
int
NC_listmap_iput(NC_listmap* listmap, size_t pos, void* obj)
{
    if(listmap == NULL) return 0;
    if(obj != NULL) {
	uintptr_t data = 0;
	const char* name = *(const char**)obj;
	if(name != NULL) return 0;
	if(pos >= nclistlength(listmap->list)) return 0;
	/* Temporarily remove from hashmap */
	if(!NC_hashmapremove(listmap->map,name,NULL))
	    return 0; /* not there */
	/* Reinsert with new pos */
	data = (uintptr_t)pos;
	NC_hashmapadd(listmap->map,(void*)data,*(char**)obj);
    }
    /* Insert at pos into listmap vector */
    nclistset(listmap->list,pos,obj);
    return 1;
}
#endif

/* Remove object from listmap; assume cast (char**)target is defined */
/* Return 1 if ok, 0 otherwise.*/
int
NC_listmap_del(NC_listmap* listmap, void* target)
{
   uintptr_t data;
   void* obj;
   if(listmap == NULL || target == NULL) return 0;
   if(!NC_hashmapget(listmap->map,*(char**)target,(void**)&data))
	return 0; /* not present */
   obj = nclistremove(listmap->list,(size_t)data);
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

/* Change data associated with a key */
/* Return 1 if ok, 0 otherwise.*/
int
NC_listmap_setdata(NC_listmap* listmap, void* obj, void* newdata)
{
   char** onamep;
   if(listmap == NULL || obj == NULL || listmap->map == NULL)
	return 0;
   onamep = (char**)obj;
   if(!NC_hashmapget(listmap->map,*onamep,NULL))
	return 0; /* not present */
   return NC_hashmapsetdata(listmap->map,*onamep,newdata);
}

/* Pseudo iterator; start index at 0, return 0 when complete.
   Usage:
      size_t iter;
      void* data	  
      for(iter=0;NC_listmap_next(listmap,iter,&data);iter++) {f(data);}
*/
size_t
NC_listmap_next(NC_listmap* listmap, size_t index, void** datap)
{
    size_t len = nclistlength(listmap->list);
    if(datap) *datap = NULL;
    if(len == 0) return 0;
    if(index >= nclistlength(listmap->list)) return 0;
    if(datap) *datap = nclistget(listmap->list,index);
    return index+1;
}

/* Reverse pseudo iterator; start index at 0, return 1 if more data, 0 if done.
   Differs from NC_listmap_next in that it iterates from last to first.
   This means that the iter value cannot be directly used as an index
   for e.g. NC_listmap_iget().
   Usage:
      size_t iter;
      void* data;
      for(iter=0;NC_listmap_next(listmap,iter,&data);iter++) {f(data);}
*/
size_t
NC_listmap_prev(NC_listmap* listmap, size_t iter, void** datap)
{
    size_t len = nclistlength(listmap->list);
    size_t index;
    if(datap) *datap = NULL;
    if(len == 0) return 0;
    if(iter >= len) return 0;
    index = (len - iter) - 1;
    if(datap) *datap = nclistget(listmap->list,index);
    return iter+1;
}

/* Rehash object with new name.
Assumes that currently obj has new name.
 */
/* Return 1 if ok, 0 otherwise.*/
int
NC_listmap_move(NC_listmap* listmap, void* obj, const char* oldname)
{ 
   const char* new_name = *(const char**)obj;
   void* data;
   /* Remove the obj from the hashtable using the oldname */
   if(!NC_hashmapremove(listmap->map, oldname, &data))
     return 0; /* not found */
   /* Reinsert using its new key */
   return NC_hashmapadd(listmap->map,data,new_name);
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

int
NC_listmap_verify(NC_listmap* lm, int dump)
{
    size_t i;
    NC_hashmap* map = lm->map;
    NClist* l = lm->list;
    size_t m;
    int nerrs = 0;

    if(dump) {
	fprintf(stderr,"-------------------------\n");
        if(map->count == 0) {
	    fprintf(stderr,"hash: <empty>\n");
	    goto next1;
	}
	for(i=0;i < map->size; i++) {
	    NC_hentry* e = &map->table[i];
	    if(e->flags != 1) continue;
	    fprintf(stderr,"hash: %d: data=%lu key=%s\n",i,(unsigned long)e->data,e->key);
	    fflush(stderr);
	}
next1:
        if(nclistlength(l) == 0) {
	    fprintf(stderr,"list: <empty>\n");
	    goto next2;
	}
	for(i=0;i < nclistlength(l); i++) {
	    const char** a = (const char**)nclistget(l,i);
	    fprintf(stderr,"list: %d: name=%s\n",i,*a);
	    fflush(stderr);
	}
	fprintf(stderr,"-------------------------\n");
	fflush(stderr);
    }

next2:
    /* Need to verify that every entry in map is also in vector and vice-versa */

    /* Verify that map entry points to same-named entry in vector */
    for(m=0;m < map->size; m++) {
	NC_hentry* e = &map->table[m];
        char** object = NULL;
	char* oname = NULL;
	uintptr_t udata = (uintptr_t)e->data;
	if((e->flags & 1) == 0) continue;
	object = nclistget(l,(size_t)udata);
        if(object == NULL) {
	    fprintf(stderr,"bad data: %d: %lu\n",(int)m,(unsigned long)udata);
	    nerrs++;
	} else {
	    oname = *object;
	    if(strcmp(oname,e->key) != 0)  {
	        fprintf(stderr,"name mismatch: %d: %lu: hash=%s list=%s\n",
			(int)m,(unsigned long)udata,e->key,oname);
	        nerrs++;
	    }
	}
    }
    /* Walk vector and mark corresponding hash entry*/
    if(nclistlength(l) == 0 || map->count == 0)
	goto done; /* cannot verify */
    for(i=0;i < nclistlength(l); i++) {
	int match;
	const char** xp = (const char**)nclistget(l,i);
        /* Walk map looking for *xp */
	for(match=0,m=0;m < map->size; m++) {
	    NC_hentry* e = &map->table[m];
	    if((e->flags & 1) == 0) continue;
	    if(strcmp(e->key,*xp)==0) {
		if((e->flags & 128) == 128) {
		    fprintf(stderr,"%d: %s already in map at %d\n",i,e->key,m);
		    nerrs++;
		}
		match = 1;
		e->flags += 128;
	    }
	}
	if(!match) {
	    fprintf(stderr,"mismatch: %d: %s in vector, not in map\n",(int)i,*xp);
	    nerrs++;
	}
    }
    /* Verify that every element in map in in vector */
    for(m=0;m < map->size; m++) {
	NC_hentry* e = &map->table[m];
	if((e->flags & 1) == 0) continue;
	if((e->flags & 128) == 128) continue;
	/* We have a hash entry not in the vector */
	fprintf(stderr,"mismatch: %d: %s->%lu in hash, not in vector\n",(int)m,e->key,(unsigned long)e->data);
	nerrs++;
    }
    /* clear the 'touched' flag */
    for(m=0;m < map->size; m++) {
	NC_hentry* e = &map->table[m];
	e->flags &= ~128;
    }

done:
    fflush(stderr);
    return (nerrs > 0 ? 0: 1);
}
