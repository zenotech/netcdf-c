#!/bin/bash
# Generate daptab.c and daptab.h from dap.y
rm -f dap.tab.c dap.tab.h	
bison --debug -d -p dap dap.y
mv dap.tab.c daptab.c; mv dap.tab.h daptab.h

