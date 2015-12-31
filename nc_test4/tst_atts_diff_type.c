#include <stdio.h>
#include <stdlib.h>
#include <netcdf.h>

/* See https://github.com/Unidata/netcdf-c/issues/128 for more
   information. */

// Fails similarly on ubuntu 14.04.02 LTS's HDF5/netcdf libraries and
// on the latest github version.
//
// Config on Ubuntu:
//   $ dpkg -l '*netcdf*'
//   /Desired=Unknown/Install/Remove/Purge/Hold
//   | Status=Not/Inst/Conf-files/Unpacked/halF-conf/Half-inst/trig-aWait/Trig-pend
//   |/ Err?=(none)/Reinst-required (Status,Err: uppercase=bad)
//   ||/ Name                               Version                Architecture           Description
//   +++-==================================-======================-======================-=========================================================================
//   ii  libnetcdf-dev                      1:4.1.3-7ubuntu2       amd64                  Development kit for NetCDF
//   un  libnetcdf4                         <none>                 <none>                 (no description available)
//   un  libnetcdf6                         <none>                 <none>                 (no description available)
//   ii  libnetcdfc++4                      1:4.1.3-7ubuntu2       amd64                  Interface for scientific data access to large binary data
//   un  libnetcdfc++5                      <none>                 <none>                 (no description available)
//   ii  libnetcdfc7                        1:4.1.3-7ubuntu2       amd64                  Interface for scientific data access to large binary data
//   ii  libnetcdff5                        1:4.1.3-7ubuntu2       amd64                  Interface for scientific data access to large binary data
//   ii  netcdf-bin                         1:4.1.3-7ubuntu2       amd64                  Programs for reading and writing NetCDF files
//   un  netcdf-doc                         <none>                 <none>                 (no description available)
//   $ dpkg -l '*hdf5*'
//   Desired=Unknown/Install/Remove/Purge/Hold
//   | Status=Not/Inst/Conf-files/Unpacked/halF-conf/Half-inst/trig-aWait/Trig-pend
//   |/ Err?=(none)/Reinst-required (Status,Err: uppercase=bad)
//   ||/ Name                               Version                Architecture           Description
//   +++-==================================-======================-======================-=========================================================================
//   ii  hdf5-helpers                       1.8.11-5ubuntu7        amd64                  Hierarchical Data Format 5 (HDF5) - Helper tools
//   un  hdf5-tools                         <none>                 <none>                 (no description available)
//   un  libhdf5-1.8                        <none>                 <none>                 (no description available)
//   un  libhdf5-1.8.4                      <none>                 <none>                 (no description available)
//   un  libhdf5-1.8.6                      <none>                 <none>                 (no description available)
//   un  libhdf5-1.8.7                      <none>                 <none>                 (no description available)
//   rc  libhdf5-7:amd64                    1.8.11-5ubuntu7        amd64                  Hierarchical Data Format 5 (HDF5) - runtime files - serial version
//   un  libhdf5-dev                        <none>                 <none>                 (no description available)
//   un  libhdf5-doc                        <none>                 <none>                 (no description available)
//   un  libhdf5-mpich2-1.8.4               <none>                 <none>                 (no description available)
//   un  libhdf5-mpich2-1.8.6               <none>                 <none>                 (no description available)
//   un  libhdf5-mpich2-1.8.7               <none>                 <none>                 (no description available)
//   ii  libhdf5-mpich2-7:amd64             1.8.11-5ubuntu7        amd64                  Hierarchical Data Format 5 (HDF5) - runtime files - MPICH2 version
//   ii  libhdf5-mpich2-dev                 1.8.11-5ubuntu7        amd64                  Hierarchical Data Format 5 (HDF5) - development files - MPICH2 version
//   un  libhdf5-serial-1.8.4               <none>                 <none>                 (no description available)
//   un  libhdf5-serial-1.8.6               <none>                 <none>                 (no description available)
//   un  libhdf5-serial-1.8.7               <none>                 <none>                 (no description available)

//
//
//
// Just compiled with:
//     g++ repro.c -o repro -lnetcdf

// using the netcdf-c from github (commit
// 219a873f8d42245b190dd5ee19ed99812dcbbc90), compiled as below and
// verified it still crashed
// g++ repro.c ~/src/netcdf/vc/netcdf-c/liblib/.libs/libnetcdf.a -lhdf5 -lhdf5_hl -lmpi -lcurl  -o repro

//
// By default, runs all seven tests.  Accepts a single optional
// integer argument MASK (which may be hex) that selects which of the
// seven tests to run.
//

#define FILENAME "tst_atts_diff_types.nc"

typedef int bool;
#define true 1
#define false 0

void
check_err(const int stat, const int line, const char *file) {
    if (stat != NC_NOERR) {
        (void)fprintf(stderr,"line %d of %s: %s\n", line, file, nc_strerror(stat));
        fflush(stderr);
        exit(1);
    }
}

void
run_test(const char *testname,
         bool v1unicode,
         bool v2unicode,
         bool deletefirst
    ) {

    int  stat;  /* return status */
    int  ncid;  /* netCDF id */

    /* group ids */
    int root_grp;

    /* dimension ids */
    int dim_dim;

    /* dimension lengths */
    size_t dim_len = 10;

    /* variable ids */
    int var_id;

    /* rank (number of dimensions) for each variable */
#   define RANK_var 1

    /* variable shapes */
    int var_dims[RANK_var];

    printf("Beginning test %s\n", testname);

    /* enter define mode */
    stat = nc_create(FILENAME, NC_CLOBBER|NC_NETCDF4, &ncid);
    check_err(stat,__LINE__,__FILE__);
    root_grp = ncid;

    /* define dimensions */
    stat = nc_def_dim(root_grp, "dim", dim_len, &dim_dim);
    check_err(stat,__LINE__,__FILE__);

    /* define variables */

    var_dims[0] = dim_dim;
    stat = nc_def_var(root_grp, "var", NC_DOUBLE, RANK_var, var_dims, &var_id);
    check_err(stat,__LINE__,__FILE__);

    /* assign per-variable attributes */
    { /* _FillValue */
    static const double var_FillValue_att[1] = {-1} ;
    stat = nc_put_att_double(root_grp, var_id, "_FillValue", NC_DOUBLE, 1, var_FillValue_att);
    check_err(stat,__LINE__,__FILE__);
    }

    printf("1st pass: calling ");
    if (v1unicode) {
        printf("nc_put_att_string");
        { /* att */
            static const char* var_att_att[1] = {"foo"} ;
            stat = nc_put_att_string(root_grp, var_id, "att", 1, var_att_att);    check_err(stat,__LINE__,__FILE__);
        }
    } else {
        printf("nc_put_att_text");
        { /* att */
            stat = nc_put_att_text(root_grp, var_id, "att", 3, "foo");
            check_err(stat,__LINE__,__FILE__);
        }
    }
    printf("\n");

    if (deletefirst) {
        printf("Deleting attribute\n");
        stat = nc_del_att(root_grp, var_id, "att");
        check_err(stat,__LINE__,__FILE__);
    }

    printf("2nd pass: calling ");
    if (v2unicode) {
        printf("nc_put_att_string");
        { /* att */
            static const char* var_att_att[1] = {"foo"} ;
            stat = nc_put_att_string(root_grp, var_id, "att", 1, var_att_att);    check_err(stat,__LINE__,__FILE__);
        }
    } else {
        printf("nc_put_att_text");
        { /* att */
            stat = nc_put_att_text(root_grp, var_id, "att", 3, "foo");
            check_err(stat,__LINE__,__FILE__);
        }
    }
    printf("\n");

    /* leave define mode */
    stat = nc_enddef (root_grp);
    check_err(stat,__LINE__,__FILE__);

    /* assign variable data */

    stat = nc_close(root_grp);
    check_err(stat,__LINE__,__FILE__);

    printf("\n");
}

int
main(int argc, char *argv[]) {
    int count = -1;
    if (argc > 1) {
        count = strtol(argv[1], NULL, 0);
        printf("TEST MASK %x\n", count);
    }

    for (int pass = 0; pass < 3; ++pass) {
        if (count < 0 || (count & (1 << 0)))
            run_test("0 SSF", false, false, false); // OKAY
        if (count < 0 || (count & (1 << 1)))
            run_test("1 UUF", true, true, false);   // OKAY
        if (count < 0 || (count & (1 << 2)))
            run_test("2 SST", false, false, true);  // OKAY
        if (count < 0 || (count & (1 << 3)))
            run_test("3 UUT", true, true, true);    // OKAY
        if (count < 0 || (count & (1 << 4)))
            run_test("4 UST", true, false, true);   // OKAY
        if (count < 0 || (count & (1 << 5)))
            run_test("5 SUT", false, true, true);   // OKAY
        if (count < 0 || (count & (1 << 6)))
            run_test("6 USF", true, false, false);
        if (count < 0 || (count & (1 << 7)))
            run_test("7 SUT", false, true, false);  // CRASH
    }

    return 0;
}
