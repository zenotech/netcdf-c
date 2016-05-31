#!/bin/sh
#set -x
set -e
#S="-s"

#Options
O="-d5 -l9 -p0 -b32 -t1e-9 -r32"

# Known compressions
ALL="zip szip bzip2 fpzip zfp"
# Ignore szip when defaulting
DFALT="zip bzip2 fpzip zfp"
BASELINE=nozip

# if this is part of a distcheck action, then this script
# will be executed in a different directory
# than the ontaining it; so capture the path to this script
# as the location of the source directory and pwd as builddir
# Compute the srcdir directory
if test "x$srcdir" = x ; then
srcdir=`dirname $0`
pushd $srcdir
srcdir=`pwd`
popd
fi

# Compute the build directory
if test "x$builddir" = x ; then
builddir=`pwd`
cd $builddir
fi

EXE="${builddir}/tst_compress"

clean() {
    for c in ${DFALT} ; do
        rm -f $c.nc $c.cdl
    done
}

compare() {
  $builddir/../ncdump/ncdump $S -n compress $1.nc > $1.cdl
  # diff against ${BASELINE}
  if diff -wBb ${BASELINE}.cdl $1.cdl ; then
    echo "   PASS: $1"
  else
    echo "   FAIL: $1"
    EXITCODE=1
  fi
}

dotest() {
  # Create {zip,bzip2,szip}.nc
  a=$1
  if ! ${EXE} "-$a" ; then
    echo "***FAIL: tst_compress: $a"
    EXITCODE=1
  else
    # If a compressor was ignored, then its nc might not exist
    if test -f $a.nc ; then
        compare $a
    fi
  fi
}

baseline() {
  rm -f ${BASELINE}.cdl ${BASELINE}.nc
  if ! ${EXE} ; then
    echo "***FAIL: tst_compress nozip"
  else
    ../ncdump/ncdump $S -n compress nozip.nc > ./${BASELINE}.cdl
  fi
}

##################################################

clean

# Build the baseline
baseline

EXITCODE=0

for c in $DFALT ; do
  dotest $c
done

NZWC=`wc -c ${BASELINE}.nc | cut -d' ' -f1`
for c in $DFALT ; do
WC=`wc -c ${c}.nc | cut -d' ' -f1`
if test $WC -ge $NZWC ; then
  echo "$c: no file reduction"
else
  echo "$c: file reduction: $NZWC -> $WC"
fi
done

clean
rm -f ${BASELINE}.nc ${BASELINE}.cdl

if test "x$EXITCODE" = "x0" ; then
    echo "***PASS: run_compress"
else
  echo "***FAIL: run_compress"
fi

exit $EXITCODE
