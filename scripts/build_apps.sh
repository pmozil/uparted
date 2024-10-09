#!/bin/sh

NUM_CPUS=$((`getconf _NPROCESSORS_ONLN` + 2))
build -n $NUM_CPUS -a X64 -t GCC -p $@
