#!/bin/sh

NUM_CPUS=$((`getconf _NPROCESSORS_ONLN` + 2))

bash ./scripts/setup_parted.sh
build -n $NUM_CPUS -a X64 -t GCC -p ./edk2-libc/AppPkg/AppPkg.dsc
