#!/bin/bash
for (( c=0; c<32; c++ ))
do
    echo "extern void trap_$c();"
    echo "SETGATE(idt[$c], 0, GD_KT, trap_$c, 0);"
done

for (( c=0; c<32; c++ ))
do
    echo "TRAPHANDLER_NOEC(trap_$c, $c)"
done