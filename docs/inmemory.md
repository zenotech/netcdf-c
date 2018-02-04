In-Memory Support in netCDF
============================
<!-- double header is needed to workaround doxygen bug -->

# In-Memory Support in netCDF {#inmemory}

__Author__: Dennis Heimbigner<br>
__Initial Version__: 2/3/2018<br>
__Last Revised__: 2/3/2018

[TOC]

## Introduction {#inmemory_intro}

It can be convenient to operate on a netcdf file whose
content is held in memory instead of in a disk file.
The netcdf API has been modified in a number of ways
to support this capability.

Actually, three distinct but related capabilities are provided.

1. DISKLESS -- Read a file into memory, operate on it, and optionally
write it back out to disk when nc_close() is called.
2. INMEMORY -- Tell the netcdf-c library to treat a provided block
of memory as if it were a netcdf file. At close, it is possible to ask
for the final contents of the memory chunk. Be warned that there is
some complexity to this as described below.
4. MMAP -- Tell the netcdf-c library to use the _mmap()_ operating
system functionality to access a file.

The first two capabilities are intertwined in the sense that the _diskless_
capability makes use internally of the _inmemory_ capability. But, the
_inmemory_ capability can be used independently of the _diskless_ capability.

The _mmap()_ capability provides a capability similar to _diskless_ but
using special capabilities of the underlying operating system.

Note also that _diskless_ and _inmemory_ can be used for both
_netcdf-3_ (classic) and _netcdf-4_ (enhanced) data. The _mmap_
capability can only be used with _netcdf-3_.

## Enabling Diskless File Access {#Enable_Diskless}
The _diskless_ capability can be used relatively transparently
using the _NC_DISKLESS_ mode flag.

Note that since the file is stored in memory, size limitations apply.
If you are on using a 32-bit pointer then the file size must be less than 2^32
bytes in length. On a 64-bit machine, the size must be less than 2^64 bytes.

### Diskless File Open
Calling _nc_open()_ using the mode flag _NC_DISKLESS_ will cause
the file being opened to be read into memory. When calling _nc_close()_,
the file will optionally be re-written (aka "persisted") to disk. This
persist capability will be invoked if and only if _NC_WRITE_ is specified
in the mode flags at the call to _nc_open()_.

### Diskless File Create
Calling _nc_create()_ using the mode flag _NC_DISKLESS_ will cause
the file to initially be created and kept in memory.
When calling _nc_close()_, the file will be written
to disk.
Note that if it is desired to create the file in memory,
but not write to a disk file, then one can either set
the NC_NOCLOBBER mode flag or one can call _nc_abort()_
instead of _nc_close()_.

## Enabling Inmemory File Access {#Enable_Inmemory}

The netcdf API has been extended to support the inmemory capability.
The relevant API is defined in the file _netcdf_mem.h_.

The important data structure to use is _NC_memio_.
````
typedef struct NC_memio {
    size_t size;
    void* memory;
    int flags;
} NC_memio;

````
An instance of this data structure is used when providing or
retrieving a block of data. It specifies the memory and its size
and also some relevant flags that define how to manage the memory.

Current only one flag is defined -- _NC_MEMIO_LOCKED_.
This tells the netcdf library that it should never try to
_realloc()_ the memory nor to _free()_ the memory. Note
that this does not mean that the memory might be modified, but
only that the modifications will be within the confines of the provided
memory. If doing such modifications is impossible without
reallocating the memory, then the modification will fail.

### In-Memory API

The new API consists of the following functions.
````
int nc_open_mem(const char* path, int mode, size_t size, void* memory, int* ncidp);

int nc_create_mem(const char* path, int mode, size_t initialsize, int* ncidp);

int nc_open_memio(const char* path, int mode, NC_memio* info, int* ncidp);

/* Close memory file and return the final memory state */
EXTERNL int nc_close_memio(int ncid, NC_memio* info);

````
### The __nc_open_mem__ Function

The _nc_open_mem()_ function is actually a convenience
function that internally invokes _nc_open_memio()_.
It essentially provides simple read-only access to a chunk of memory
of some specified size.

### The __nc_open_memio__ Function

This function provides a more general read/write capability with respect
to a chunk of memory. It has a number of constraints and its
semantics are somewhat complex. This is primarily due to limitations
imposed by the underlying HDF5 library.

The constraints are as follows.

1. If the _NC_MEMIO_LOCKED_ flag is set, then the netcdf library will
make no attempt to reallocate or free the provided memory.
If the caller invokes the _nc_close_memio()_ function to retrieve the
final memory block, it should be the same
memory block as was provided when _nc_open_memio_ was called.
Note that it is still possible to modify the in-memory file if the NC_WRITE
mode flag was set. However, failures can occur if an operation
cannot complete because the memory needs to be expanded.
2. If the _NC_MEMIO_LOCKED_ flag is <b>not</b> set, then
the netcdf library will feel free to reallocate the provided
memory block to obtain a larger block when an attempt to modify
the in-memory file requires more space. Note that implicit in this
is that the old block -- the one originally provided -- may be
free'd as a side effect of re-allocating the memory using the
_realloc()_ function.
If the caller invokes the _nc_close_memio()_ function to retrieve the
final memory block, there is no guarantee that the returned block is the
same as the memory block as was provided when _nc_open_memio_ was called.
If they differ, then that means the original block was free'd and the caller
should not attempt to free it again.

## Enabling MMAP File Access {#Enable_MMAP}

Some operating systems provide a capability called MMAP.
This allows disk files to automatically be mapped to chunks of memory.
It operates in a fashion somewhat similar to operating system virtual
memory, except with respect to a file.

By setting mode flag NC_MMAP, it is possible to do the equivalent
of NC_DISKLESS but using the operating system's mmap capabilities.

Currently, MMAP support is only available when using netcdf-3 or cdf5
files.

## References {#References}

1. https://support.hdfgroup.org/HDF5/doc1.8/Advanced/FileImageOperations/HDF5FileImageOperations.pdf

