#ifndef HASHMAP_H_INCLUDED
#define HASHMAP_H_INCLUDED

/*
Data is presumed to be an index into some other table
   Assume it can be compared using simple ==
Key is some hash of some variable length data;
   presume we can compare for equality using memcmp
The variable length data is pointed to
by a char*.
*/

/*
For all objects that are insertable
into a hash table, the first
element of the object must be this
struct so we can legally cast the object
to this struct.
*/
#define NC_hobject NC_string

/*! Hashmap-related structs.
  NOTE: 'data' is the dimid or varid which is non-negative.
  we store the dimid+1 so a valid entry will have
  data > 0
*/
typedef struct NC_hentry {
    int flags;
    size_t data;
    size_t hashkey; /* Hash id */
} NC_hentry;

/*
The hashmap object must give us the hash table (table),
the |table| size, the # of defined entries in the table,
and a pointer to the vector of objects.
*/
typedef struct NC_hashmap {
  size_t size;
  size_t count;
  NC_hentry* table;
  NC_hobject** objects;
} NC_hashmap;

/* defined in nc_hashmap.c */

/** Creates a new hashmap near the given size. */
extern NC_hashmap* NC_hashmapcreate(size_t startsize, NC_hobject**);

/** Inserts a new element into the hashmap. */
/* Note we pass the NC_hobjecty struct by value */
extern void NC_hashmapadd(NC_hashmap*, size_t data, const NC_hobject);

/** Removes the storage for the element of the key.
    Return 1 if found, 0 otherwise; returns the data in datap if !null
*/
extern int NC_hashmapremove(NC_hashmap*, const NC_hobject, size_t* datap);

/** Returns the data for the key.
    Return 1 if found, 0 otherwise; returns the data in datap if !null
*/
extern int NC_hashmapget(NC_hashmap*, const NC_hobject, size_t* datap);

/** Returns the number of saved elements. */
extern size_t NC_hashmapCount(NC_hashmap*);

/** Reclaims the hashmap structure. */
extern void NC_hashmapfree(NC_hashmap*);

#endif
