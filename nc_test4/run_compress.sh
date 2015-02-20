#!/bin/sh
#set -x
set -e
#S="-s"

#Options
O="-d5 -l9 -p0 -b32 -t1e-9 -r32"

# Known compressions
C0="nozip zip szip bzip2 fpzip zfp"
# Ignore szip when defaulting
DFALT="zip bzip2 fpzip zfp"

BASELINE=nozip

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
cd ${srcdir}/..
top_srcdir=`pwd`
cd $builddir
echo "top_srcdir=${top_srcdir}"

# Figure out the compressions to test
if test -f ${top_srcdir}/libnetcdf.settings ; then
  TC=`cat ${top_srcdir}/libnetcdf.settings | fgrep -i 'filter support'`
  TC=`echo "${TC}" | tr ',' ' ' | cut -d: -f 2`
#sed -e 's/^Filter Support:[ \t]*\(.*\)$/\1/p'`
else
  TC="${DFALT}"
fi
TC=`echo ${TC} | sed 's/^ *//'`

if test -f "${builddir}/tst_compress.exe" ; then
EXE="${builddir}/tst_compress.exe"
else
EXE="${builddir}/tst_compress"
fi

clean() {
    for c in ${DFALT} ; do
        rm -f $c.nc $c.cdl
    done
}

compare() {
  $builddir/../ncdump/ncdump $S -n compress $1.nc > $1.cdl
  # diff against ${BASELINE}
  if diff -wBb ${srcdir}/${BASELINE}.cdl $1.cdl ; then
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

dotest() {
  # Create {zip,bzip2,szip}.nc
  if ! ${EXE} ${O} $1 ; then
    echo "***FAIL: tst_compress: $1"
    PASSFAIL=0
  else
    # If a compressor was ignored, then its nc might not exist
    if test -f $1.nc ; then
        compare $1
    fi
  fi
}

baseline() {
  rm -f ${BASELINE}.cdl ${BASELINE}.nc
  if ! ${EXE} ${O} nozip ; then
    echo "***FAIL: tst_compress zip"
  else
    ../ncdump/ncdump $S -n compress nozip.nc > ./${BASELINE}.cdl
  fi
}

##################################################

clean

# Build the baseline
baseline


PASSFAIL=1

for c in $TC ; do
  dotest $c
done

clean
rm -f ${BASELINE}.nc ${BASELINE}.cdl

if test "x$PASSFAIL" = "x1" ; then
    echo "***PASS: run_compress"
    CODE=0
else
  echo "***FAIL: run_compress"
  CODE=1
fi

exit $CODE
