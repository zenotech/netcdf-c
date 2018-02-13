/*
Copyright (c) 1998-2017 University Corporation for Atmospheric Research/Unidata
See LICENSE.txt for license information.
*/

#ifndef NCLISTMAP_H
#define NCLISTMAP_H

#include "nclist.h"
#include "nchashmap.h" /* Also includes name map and id map */

/* Forward (see nc4internal.h)*/
struct NC_OBJ;

/*
This listmap data structure is an ordered list of objects. It is
used pervasively in libsrc to store metadata relationships.  The
goal is to provide both by-name (via NC_hashmap), by-id (via NC_hashmap)
and indexed access (via NClist) to the objects in the listmap.  Using
hashmap might be overkill for some relationships, but we can
sort that out later.
As a rule, we use this to store definitional relationships
such as (in groups) dimension definitions, variable definitions, type defs
and subgroup defs. We do not, as a rule, use this to store reference relationships
such as the list of dimensions for a variable.
*/

/* Generic list + matching hashtable */
typedef struct NC_listmap {
   NClist* list;
   NC_hashmap* namemap;
   NC_hashmap* idmap;
} NC_listmap;

/* Locate object by name in an NC_listmap */
extern struct NC_OBJ* NC_listmap_get(NC_listmap* listmap, const char* name);

/* Locate object by id in an NC_listmap */
extern struct NC_OBJ* NC_listmap_iget(NC_listmap* listmap, size_t id);

/* Get ith object in the list map vector */
extern void* NC_listmap_ith(NC_listmap* listmap, size_t index);

/* Add object to the end of the vector, also insert into the hashmaps; */
/* Return 1 if ok, 0 otherwise.*/
extern int NC_listmap_add(NC_listmap* listmap, struct NC_OBJ* obj);

/* Get a copy of the vector contents */
extern struct NC_OBJ** NC_listmap_dup(NC_listmap* listmap);

/* Rehash all objects in the vector */
/* Return 1 if ok, 0 otherwise.*/
extern int NC_listmap_rehash(NC_listmap* listmap);

#if 0
/* Remove object from listmap */
/* Return 1 if ok, 0 otherwise.*/
extern int NC_listmap_del(NC_listmap* listmap, struct NC_OBJ* target);

/* Remove object from listmap by index in vector*/
/* Return 1 if ok, 0 otherwise.*/
extern int NC_listmap_delith(NC_listmap* listmap, size_t index);

/* Reinsert an object into the hash maps as determined by its current vector position */
/* Return 1 if ok, 0 otherwise.*/
extern int NC_listmap_reinsert(NC_listmap* listmap, NC_OBJ* obj);
#endif

/* Pseudo iterator; start index at 0, return 1 if more data, 0 if done.
   Usage:
      size_t iter;
      void* data;
      for(iter=0;NC_listmap_next(listmap,iter,&data);iter++) {f(data);}
*/
extern size_t NC_listmap_next(NC_listmap*, size_t iter, struct NC_OBJ** datap);

#if 0
/* Reverse pseudo iterator; start index at 0, return 1 if more data, 0 if done.
   Differs from NC_listmap_next in that it iterates from last to first.
   This means that the iter value cannot be directly used as an index
   for e.g. NC_listmap_iget().
   Usage:
      size_t iter;
      void* data;
      for(iter=0;NC_listmap_prev(listmap,iter,&data);iter++) {f(data);}
*/
extern size_t NC_listmap_prev(NC_listmap* listmap, size_t iter, void** datap);
#endif

/* Reset a list map without free'ing the map itself */
/* Return 1 if ok; 0 otherwise */
extern int NC_listmap_clear(NC_listmap* listmap);

/* Initialize a list map without malloc'ing the map itself */
/* Return 1 if ok; 0 otherwise */
extern int NC_listmap_init(NC_listmap* listmap, size_t initsize);

extern int NC_listmap_verify(NC_listmap* lm, int dump);

/* Inline functions */

/* Test if map has been initialized */
#define NC_listmap_initialized(listmap) ((listmap)->list != NULL)

/* Get number of entries in a listmap */
/* size_t NC_listmap_size(NC_listmap* listmap) */
#define NC_listmap_size(listmap) ((listmap)==NULL?0:(nclistlength((listmap)->list)))

/* Test if object is in list map */
/* extern int NC_listmap_contains(NC_listmap* listmap, struct NC_OBJ* obj); */
#define NC_listmap_contains(listmap,obj) ((listmap)==NULL:0:\
					 (NC_listmap_iget((listmap),(struct NC_OBJ*)(obj))->id) != NULL)

#endif /*NCLISTMAP_H*/
