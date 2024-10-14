#!/bin/sh

NUM_CPUS=$((`getconf _NPROCESSORS_ONLN` + 2))

bash ./scripts/setup_parted.sh
bash ./scripts/setup_demo_app.sh
bash ./scripts/setup_app_pkg.sh
build -n $NUM_CPUS -a X64 -t GCC -p ./edk2-libc/AppPkg/AppPkg.dsc
