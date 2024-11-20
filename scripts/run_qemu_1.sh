#!/bin/sh

if [ -z $OVMF_PATH ]; then
    echo "Need to set \$OVMF_PATH - a path to OVMF.fd";
    exit;
fi

USR=$USER

sudo losetup /dev/loop0 ./data/image.img
sudo chown $USR /dev/loop0

qemu-system-x86_64 -enable-kvm \
    -blockdev node-name=q1,driver=raw,file.filename=/dev/loop0,file.driver=host_device \
    -drive if=pflash,format=raw,file=$OVMF_PATH \
    -drive format=raw,file=fat:rw:$1 \
    -device virtio-blk,drive=q1 \
    -nographic \
    -net none \
    -m 4096M \
    -cpu host \
    -smp 4

sudo losetup -d /dev/loop0
