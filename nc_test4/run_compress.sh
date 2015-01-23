#!/bin/sh
#set -x
set -e
#S="-s"

# Compressions to test
TC="nozip zip szip bzip2 fpzip zfp"

#Options
O="-P -d5 -l9 -p0 -b32 -t1e-9 -r32"

# Known compressions
C="nozip zip szip bzip2 fpzip zfp"

# if this is part of a distcheck action, then this script
# will be executed in a different directory
# than the ontaining it; so capture the path to this script
# as the location of the source directory and pwd as builddir
# Compute the build directory
builddir=`pwd`
echo "builddir=${builddir}"
srcdir=`dirname $0`
cd $srcdir
srcdir=`pwd`
echo "srcdir=${srcdir}"
cd $builddir

if test -f "${builddir}/tst_compress.exe" ; then
EXE="${builddir}/tst_compress.exe"
else
EXE="${builddir}/tst_compress"
fi

function clean {
for c in $C ; do
rm -f $c.nc $c.cdl
done
}

function compare {
  $builddir/../ncdump/ncdump $S -n compress $1.nc > $1.cdl
  # diff against compress.cdl
  if diff -wBb ${srcdir}/compress.cdl $1.cdl ; then
#  if test x = x ; then
    CODE=1
  else
    CODE=0
  fi
  if test "x$CODE" = "x1" ; then
    echo "   PASS: $1"
  else
    echo "   FAIL: $1"
  fi
  if test CODE = 0 ; then PASSFAIL=0; fi
}

function dotest {
  # Create {zip,bzip2,szip}.nc
  if ! ${EXE} ${O} $1 ; then
    echo "***FAIL: tst_compress: $1"
    PASSFAIL=0
  else
    compare $1
  fi
}

function baseline {
  if ! ${EXE} ${O} zip ; then
    echo "***FAIL: tst_compress zip"
  else
    rm -f compress.cdl
    ../ncdump/ncdump $S -n compress zip.nc > ./compress.cdl
  fi
}

##################################################

clean

if test "x$1" = xbaseline ; then
  baseline
  exit 0
fi

# Main test
if test -f ${srcdir}/compress.cdl ; then
    echo "baseline: ${srcdir}/compress.cdl"
else
  echo "baseline: ${srcdir}/compress.cdl"
  echo "No baseline file exists"
  exit 1
fi

PASSFAIL=1

for c in $TC ; do
  dotest $c
done

clean

if test "x$PASSFAIL" = "x1" ; then
    echo "***PASS: run_compress"
    CODE=0
else
  echo "***FAIL: run_compress"
  CODE=1
fi

exit $CODE
