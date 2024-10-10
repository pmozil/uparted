#!/bin/sh


rm -rf ./edk2-libc/AppPkg/Applications/Parted
cp -r ./parted/ ./edk2-libc/AppPkg/Applications/Parted

rm -f ./edk2-libc/AppPkg/AppPkg.dsc
cp ./parted/AppPkg.dsc ./edk2-libc/AppPkg/AppPkg.dsc
