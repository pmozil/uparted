#!/bin/sh

if [ -z $OVMF_PATH ]; then
    echo "Need to set \$OVMF_PATH - a path to OVMF.fd";
    exit;
fi

qemu-system-x86_64 -drive if=pflash,format=raw,file=$OVMF_PATH \
    -drive format=raw,file=fat:rw:$1 \
    -nographic \
    -net none
