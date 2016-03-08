
#include <stdio.h>
#include <netcdf.h>

int main() {
  int ncid, iok, i;
  int n_attrs = 30;
  char myattr[10];
  int attrval;

  iok = nc_create("tester.nc", NC_NETCDF4, &ncid);
  printf("create iok= %d\n",iok);

  for (i=0; i<n_attrs; i++) {
    sprintf(myattr,"%s%4.4d","myattr",i+1);
    attrval = i + 1;
    iok = nc_put_att(ncid,NC_GLOBAL,myattr,NC_INT,1,&attrval);
    printf("put iok= %d %d\n",iok,attrval);
  }

  iok = nc_close(ncid);
  printf("close iok= %d\n",iok);

  iok = nc_open("tester.nc", NC_WRITE, &ncid);
  printf("open iok= %d\n", iok);

  iok = nc_redef(ncid);
  printf("redef iok= %d\n", iok);

  for (i=0; i<n_attrs; i++) {
    sprintf(myattr,"%s%4.4d","myattr",i+1);
    attrval = i + 1;
    iok = nc_put_att(ncid,NC_GLOBAL,myattr,NC_INT,1,&attrval);
    printf("put iok= %d %d\n",iok,attrval);
  }

  iok = nc_enddef(ncid);
  printf("endef iok= %d\n", iok);

  iok = nc_close(ncid);
  printf("close iok= %d\n",iok);
  return 0;
}
