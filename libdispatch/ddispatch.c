#include "ncdispatch.h"
#include "ncuri.h"
#ifdef USE_DAP
#include "oc.h"
#endif

extern int NCSUBSTRATE_initialize(void);
extern int NCSUBSTRATE_finalize(void);

/* Define vectors of zeros and ones for use with various nc_get_varX function*/
size_t nc_sizevector0[NC_MAX_VAR_DIMS];
size_t nc_sizevector1[NC_MAX_VAR_DIMS];
ptrdiff_t nc_ptrdiffvector1[NC_MAX_VAR_DIMS];
size_t NC_coord_zero[NC_MAX_VAR_DIMS];
size_t NC_coord_one[NC_MAX_VAR_DIMS];

/*
static nc_type longtype = (sizeof(long) == sizeof(int)?NC_INT:NC_INT64);
static nc_type ulongtype = (sizeof(unsigned long) == sizeof(unsigned int)?NC_UINT:NC_UINT64);
*/

/* Allow dispatch to do general initialization and finalization */
int
NCDISPATCH_initialize(void)
{
    int status = NC_NOERR;
    int i;
    for(i=0;i<NC_MAX_VAR_DIMS;i++) {
	nc_sizevector0[i] = 0;
        nc_sizevector1[i] = 1;
        nc_ptrdiffvector1[i] = 1;
    }
    for(i=0;i<NC_MAX_VAR_DIMS;i++) {
	NC_coord_one[i] = 1;
	NC_coord_zero[i] = 0;
    }
    return status;
}

int
NCDISPATCH_finalize(void)
{
    int status = NC_NOERR;
    int i;
    return status;
}


#if 0 /* Apparently never used */
/* Override dispatch table management */
static NC_Dispatch* NC_dispatch_override = NULL;

/* Override dispatch table management */
NC_Dispatch*
NC_get_dispatch_override(void) {
    return NC_dispatch_override;
}

void NC_set_dispatch_override(NC_Dispatch* d)
{
    NC_dispatch_override = d;
}
#endif

/* Overlay by treating the tables as arrays of void*.
   Overlay rules are:
        overlay    base    merge
        -------    ----    -----
          null     null     null
          null      y        y
           x       null      x
           x        y        x
*/

int
NC_dispatch_overlay(const NC_Dispatch* overlay, const NC_Dispatch* base, NC_Dispatch* merge)
{
    void** voverlay = (void**)overlay;
    void** vmerge;
    int i;
    size_t count = sizeof(NC_Dispatch) / sizeof(void*);
    /* dispatch table must be exact multiple of sizeof(void*) */
    assert(count * sizeof(void*) == sizeof(NC_Dispatch));
    *merge = *base;
    vmerge = (void**)merge;
    for(i=0;i<count;i++) {
        if(voverlay[i] == NULL) continue;
        vmerge[i] = voverlay[i];
    }
    /* Finally, the merge model should always be the overlay model */
    merge->model = overlay->model;
    return NC_NOERR;
}

int
ncstrncmp(const char* s1, const char* s2, size_t len)
{
    int i;

    if(s1 == NULL) return (s2 == NULL ? 0 : -1);
    if(s2 == NULL) return 1;

    for(i=0;i<len;i++) {
	int diff;
	if(s1[i] == 0) return (s2[i] == 0 ? 0 : -1);
	else if(s2[i] == 0) return +1;
	diff = (s1[i] - s2[i]);
	if(diff != 0) return diff;
    }
    return 0;		
}

