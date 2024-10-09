# Uparted - a port of GNU parted to UEFI

This repo is a port of uparted to UEFI, including the scripts to build it.

## Requirements

libuuid libuuid-devel nasm acpica-tools

## Building

To start, you need to set up the repository.
Do this:

```sh
git clone https://github.com/pmozil/uparted
cd uparted
. scripts/setup_repo.sh
```

This inits and updates the submodules.
