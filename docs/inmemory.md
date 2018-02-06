In-Memory Support in netCDF
============================
<!-- double header is needed to workaround doxygen bug -->

# In-Memory Support in netCDF {#inmemory}

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
that this does not mean that the memory cannot be modified, but
only that the modifications will be within the confines of the provided
memory. If doing such modifications is impossible without
reallocating the memory, then the modification will fail.

### In-Memory API

The new API consists of the following functions.
````
int nc_open_mem(const char* path, int mode, size_t size, void* memory, int* ncidp);

int nc_create_mem(const char* path, int mode, size_t initialsize, int* ncidp);

int nc_open_memio(const char* path, int mode, NC_memio* info, int* ncidp);

int nc_close_memio(int ncid, NC_memio* info);

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

### The __nc_create_mem__ Function

This function allows a user to create an in-memory file, write to it,
and then retrieve the final memory using _nc_close_memio()_.
The _initialsize_ argument to _nc_create_mem()_ tells the library
how much initial memory to allocate. Technically, this is advisory only
because it may be ignored by the underlying HDF5 library.
It is used, however, for netcdf-3 files. 

### The __nc_close_memio__ Function

The ordinary _nc_close()_ function can be called to close an in-memory file.
However, it is often desirable to obtain the final size and memory block
for the in-memory file when that file has been modified.
The _nc_close_memio()_ function provides a means to do this.
Its second argument is a pointer to an _NC_memio_ object
into which the final memory and size are stored. WARNING,
the returned memory is owned by the caller and so the caller
is responsible for calling _free()_ on that returned memory.

### Support for Writing with NC_MEMIO_LOCKED

When the NC_MEMIO_LOCKED flag is set in the _NC_memio_ object
passed to _nc_open_memio()_, it is still possible to modify
the opened in-memory file (using the NC_WRITE mode flag).

The big problem is that any changes must fit into the memory provided
by the caller via the _NC_memio_ object. This problem can be
mitigated, however, by using the "trick" of overallocating
the caller supplied memory. That is, if the original file is, say, 300 bytes,
then it is possible to allocate, say, 65000 bytes and copy the original file
into the first 300 bytes of the larger memory block. This will allow
the netcdf-c library to add to the file up to that 65000 byte limit.
In this way, it is possible to avoid memory reallocation while still
allowing modifications to the file. You will still need to call
_nc_close_memio()_ to obtain the size of the final, modified, file.

## Enabling MMAP File Access {#Enable_MMAP}

Some operating systems provide a capability called MMAP.
This allows disk files to automatically be mapped to chunks of memory.
It operates in a fashion somewhat similar to operating system virtual
memory, except with respect to a file.

By setting mode flag NC_MMAP, it is possible to do the equivalent
of NC_DISKLESS but using the operating system's mmap capabilities.

Currently, MMAP support is only available when using netcdf-3 or cdf5
files.

## Known Bugs {#Inmemory_Bugs}

1. If you are modifying a locked memory chunk (using
   NC_MEMIO_LOCKED) and are accessing it as a netcdf-4 file, and
   you overrun the available space, then the HDF5 library will
   fail with a segmentation fault.

## References {#Inmemory_References}

1. https://support.hdfgroup.org/HDF5/doc1.8/Advanced/FileImageOperations/HDF5FileImageOperations.pdf

## Point of Contact

__Author__: Dennis Heimbigner<br>
__Email__: dmh at ucar dot edu
__Initial Version__: 2/3/2018<br>
__Last Revised__: 2/5/2018

