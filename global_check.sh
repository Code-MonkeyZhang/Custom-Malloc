#!/bin/bash

if [ -f mm.o ]
then
    nm -f posix mm.o | grep " [BbCcDdGgSsVvWw] " | awk 'BEGIN{total=0; out="";} {size=strtonum("0x" $4); total += size; out = out size "\t" $1 "\n";} END{out = "ERROR: Using more than 128 bytes of global memory\nSize\tVariable\n----\t--------\n" out "----------------\n" total "\tTOTAL"; if (total > 128) {print out}}'
else
    echo "ERROR: Must successfully compile code before running script"
fi
