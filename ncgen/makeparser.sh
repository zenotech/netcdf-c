#!/bin/bash

# These rules are used if someone wants to rebuild ncgenyy.c or ncgentab.c
# Otherwise never invoked, but records how to do it.
# BTW: note that renaming is essential because otherwise
# autoconf will forcibly delete files of the name *.tab.*

flex -L -Pncg -8 ncgen.l
rm -f ncgenl.c
sed -e s/lex.ncg.c/ncgenl.c/g <lex.ncg.c >ncgenl.c
bison -pncg -t -d ncgen.y
rm -f ncgeny.c ncgeny.h
sed -e s/ncgen.tab.c/ncgeny.c/g -e s/ncgen.tab.h/ncgeny.h/g <ncgen.tab.c >ncgeny.c
sed -e s/ncgen.tab.c/ncgeny.c/g -e s/ncgen.tab.h/ncgeny.h/g <ncgen.tab.h >ncgeny.h
rm -f ncgen.tab.c ncgen.tab.h lex.ncg.c
