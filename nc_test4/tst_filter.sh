#!/bin/sh

export SETX=1
set -x

if test "x$srcdir" = x ; then srcdir=`pwd`; fi
. ../test_common.sh

set -e

# Which test cases to exercise
API=1
NG=1
NCP=1
UNK=1
NGC=1
MISC=1

# Function to remove selected -s attributes from file;
# These attributes might be platform dependent
sclean() {
cat $1 \
  | sed -e '/var:_Endianness/d' \
  | sed -e '/_NCProperties/d' \
  | sed -e '/_SuperblockVersion/d' \
  | sed -e '/_IsNetcdf4/d' \
  | cat > $2
}

# Function to extract _Filter attribute from a file
# These attributes might be platform dependent
getfilterattr() {
sed -e '/var:_Filter/p' -ed <$1 >$2
}

trimleft() {
sed -e 's/[ 	]*\([^ 	].*\)/\1/' <$1 >$2
}

# So are we operating under CYGWIN? (test using uname)
tcc_os=`uname | cut -d '_'  -f 1`
if test "x$tcc_os" = xCYGWIN ; then ISCYGWIN=1; fi

# Figure out the plugin file name
# Test for visual studio before cygwin
if test "x$ISMSVC" != x ; then
  BZIP2LIB=bzip2.dll
  MISCLIB=misc.dll
elif test "x$ISCYGWIN" != x ; then
  BZIP2LIB=cygbzip2.dll
  MISCLIB=cygmisc.dll
else
  BZIP2LIB=libbzip2.so
  MISCLIB=libmisc.so
fi

# Figure out the plugin path
# Hopefully somewhere below this dir
# This can probably be simplified
HDF5_PLUGIN_PATH=
PLUGINPATH="$WD/hdf5plugins"
# Case 1: Cmake with Visual Studio
if test "x$ISCMAKE" != x -a "x${ISMSVC}" != x ; then
  # Case 1a: try the build type directory e.g. Release
  if test -f "${PLUGINPATH}/$VSCFG/${BZIP2LIB}" ; then
    HDF5_PLUGIN_PATH="${PLUGINPATH}/$VSCFG"
  else # Case 1b Ignore the build type dir
    if test -f "${PLUGINPATH}/${BZIP2LIB}" ; then
      HDF5_PLUGIN_PATH="${PLUGINPATH}"
    fi
  fi
else # Case 2: automake
  # Case 2a: look in .libs
  if test -f "${PLUGINPATH}/.libs/${BZIP2LIB}" ; then
    HDF5_PLUGIN_PATH="${PLUGINPATH}/.libs"
  fi # No case 2b
fi

# Did we find it?
if test "x$HDF5_PLUGIN_PATH" = x ; then
  # Not found, fail
  echo "***Fail: Could not locate a usable HDF5_PLUGIN_PATH"
  exit 1
fi

# If we are operating using both Visual Studio and Cygwin,
# then we need to convert the HDF5_PLUGIN_PATH to windows format
if test "x$ISMSVC" != x -a "x$ISCYGWIN" != x ; then
HDF5_PLUGIN_PATH=`cygpath -wl $HDF5_PLUGIN_PATH`
fi

echo "final HDF5_PLUGIN_PATH=$HDF5_PLUGIN_PATH}"
export HDF5_PLUGIN_PATH

# Verify
BZIP2PATH="${HDF5_PLUGIN_PATH}/${BZIP2LIB}"
if ! test -f ${BZIP2PATH} ; then
echo "Unable to locate ${BZIP2PATH}"
exit 1
fi


# Execute the specified tests

if test "x$API" = x1 ; then
echo "*** Testing dynamic filters using API"
rm -f ./bzip2.nc ./bzip2.dump ./tmp
${execdir}/test_filter
${NCDUMP} -s bzip2.nc > ./tmp
# Remove irrelevant -s output
sclean ./tmp ./bzip2.dump
diff -b -w ${srcdir}/bzip2.cdl ./bzip2.dump
echo "*** Pass: API dynamic filter"
fi

if test "x$MISC" = x1 ; then
echo
echo "*** Testing dynamic filters parameter passing"
rm -f ./testmisc.nc tmp tmp2
${execdir}/test_filter_misc
# Verify the parameters via ncdump
${NCDUMP} -s testmisc.nc > ./tmp
# Extract the parameters
getfilterattr ./tmp ./tmp2
rm -f ./tmp
trimleft ./tmp2 ./tmp
rm -f ./tmp2
cat >./tmp2 <<EOF
var:_Filter = "32768,1,4294967279,23,4294967271,27,77,93,1145389056,3287505826,1097305129,1,2147483648,4294967295,4294967295" ;
EOF
diff -b -w ./tmp ./tmp2
echo "*** Pass: parameter passing"
fi

if test "x$NG" = x1 ; then
echo "*** Testing dynamic filters using ncgen"
rm -f ./bzip2.nc ./bzip2.dump ./tmp
${NCGEN} -lb -4 -o bzip2.nc ${srcdir}/bzip2.cdl
${NCDUMP} -s bzip2.nc > ./tmp
# Remove irrelevant -s output
sclean ./tmp ./bzip2.dump
diff -b -w ${srcdir}/bzip2.cdl ./bzip2.dump
echo "*** Pass: ncgen dynamic filter"
fi

if test "x$NCP" = x1 ; then
echo "*** Testing dynamic filters using nccopy"
rm -f ./unfiltered.nc ./filtered.nc ./filtered.dump ./tmp
${NCGEN} -4 -lb -o unfiltered.nc ${srcdir}/unfiltered.cdl
${NCCOPY} -F "/g/var,307,9,4" unfiltered.nc filtered.nc
${NCDUMP} -s filtered.nc > ./tmp
# Remove irrelevant -s output
sclean ./tmp ./filtered.dump
diff -b -w ${srcdir}/filtered.cdl ./filtered.dump
echo "*** Pass: nccopy dynamic filter"
fi

if test "x$UNK" = x1 ; then
echo "*** Testing access to filter info when filter dll is not available"
rm -f bzip2.nc ./tmp
# build bzip2.nc
${NCGEN} -lb -4 -o bzip2.nc ${srcdir}/bzip2.cdl
# dump and clean bzip2.nc header only when filter is avail
${NCDUMP} -hs bzip2.nc > ./tmp
# Remove irrelevant -s output
sclean ./tmp bzip2.dump
# Now hide the filter code
mv ${BZIP2PATH} ${BZIP2PATH}.save
# dump and clean bzip2.nc header only when filter is not avail
rm -f ./tmp
${NCDUMP} -hs bzip2.nc > ./tmp
# Remove irrelevant -s output
sclean ./tmp bzip2x.dump
# Restore the filter code
mv ${BZIP2PATH}.save ${BZIP2PATH}
diff -b -w ./bzip2.dump ./bzip2x.dump
echo "*** Pass: ncgen dynamic filter"
fi

if test "x$NGC" = x1 ; then
rm -f ./test_bzip2.c
echo "*** Testing dynamic filters using ncgen with -lc"
${NCGEN} -lc -4 ${srcdir}/bzip2.cdl > test_bzip2.c
diff -b -w ${srcdir}/ref_bzip2.c ./test_bzip2.c
echo "*** Pass: ncgen dynamic filter"
fi

#cleanup
rm -f ./bzip*.nc ./unfiltered.nc ./filtered.nc ./tmp ./tmp2 *.dump bzip*hdr.*
rm -fr ./test_bzip2.c
rm -fr ./testmisc.nc

echo "*** Pass: all selected tests passed"

exit 0
