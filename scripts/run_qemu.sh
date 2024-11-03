#!/bin/sh

if [ -z $OVMF_PATH ]; then
    echo "Need to set \$OVMF_PATH - a path to OVMF.fd";
    exit;
fi

qemu-system-x86_64 -enable-kvm \
    -drive if=pflash,format=raw,file=$OVMF_PATH \
    -drive format=raw,file=fat:rw:$1 \
    -drive format=raw,file=./data/image.img \
    -nographic \
    -net none \
    -m 4096M \
    -cpu host \
    -smp 4
