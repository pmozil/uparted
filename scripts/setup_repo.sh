#!/bin/sh

git submodule init

cd edk2
git submodule update --init --recursive --jobs=$(nproc)
make -C BaseTools -j $(nproc)
cd ..

cd edk2-libc
git submodule update --init --recursive --jobs=$(nproc)
cd ..
#
# cp -r $(realpath edk2-libc/StdLibPrivateInternalFiles) $(realpath edk2)/StdLibPrivateInternalFiles
# cp -r $(realpath edk2-libc/StdLib) $(realpath edk2)/StdLib
# cp -r $(realpath edk2-libc/AppPkg) $(realpath edk2)/AppPkg
