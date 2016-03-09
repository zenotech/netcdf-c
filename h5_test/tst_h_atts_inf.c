/* This is part of the netCDF package.
   Copyright 2016 University Corporation for Atmospheric Research/Unidata
   See COPYRIGHT file for conditions of use.

   Test HDF5 file code. These are not intended to be exhaustive tests,
   but they use HDF5 the same way that netCDF-4 does, so if these
   tests don't work, than netCDF-4 won't work either.

   This file creates 30 global attributes and then closes the
   file and exits.  This is done to test for an 'infinite loop'
   error thrown by libhdf5.

   See https://github.com/Unidata/netcdf-c/issues/233 for more info.

*/
