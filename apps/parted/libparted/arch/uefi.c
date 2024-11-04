#include <config.h>

#include <parted/debug.h>
#include <parted/parted.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#include "../architecture.h"

typedef struct _GNUSpecific GNUSpecific;

struct _GNUSpecific {
    struct store *store;
    int consume;
};

/* Initialize a PedDevice using SOURCE.  The SOURCE will NOT be destroyed;
   the caller created it, it is the caller's responsilbility to free it
   after it calls ped_device_destory.  SOURCE is not registered in Parted's
   list of devices.  */
PedDevice *ped_device_new_from_store(struct store *source);

static int _device_get_sector_size(PedDevice *dev) {
    EFI_BLOCK_IO_PROTOCOL *block_io =
        (EFI_BLOCK_IO_PROTOCOL *)dev->arch_specific;
    return block_io->Media->BlockSize;
}

static PedSector _device_get_length(PedDevice *dev) {
    EFI_BLOCK_IO_PROTOCOL *block_io =
        (EFI_BLOCK_IO_PROTOCOL *)dev->arch_specific;
    // UINT64 disk_size =
    //     (block_io->Media->LastBlock + 1) * block_io->Media->BlockSize;
    return block_io->Media->LastBlock + 1;
}

static int _device_probe_geometry(PedDevice *dev) {
    PedSector cyl_size;

    dev->length = _device_get_length(dev);
    if (!dev->length)
        return 0;

    dev->sector_size = _device_get_sector_size(dev);
    if (!dev->sector_size)
        return 0;

    /* XXX: We have no way to get this!  */
    dev->bios_geom.sectors = 63;
    dev->bios_geom.heads = 255;
    cyl_size = dev->bios_geom.sectors * dev->bios_geom.heads;
    dev->bios_geom.cylinders =
        dev->length / cyl_size * (dev->sector_size / PED_SECTOR_SIZE_DEFAULT);
    dev->hw_geom = dev->bios_geom;

    return 1;
}

/* Initialize by allocating memory and filling in a few defaults, a
   PedDevice structure.  */
static PedDevice *_init_device(const char *path) {
    PedDevice *dev;
    GNUSpecific *arch_specific;

    dev = (PedDevice *)ped_malloc(sizeof(PedDevice));
    if (!dev)
        goto error;

    dev->path = strdup(path);
    if (!dev->path)
        goto error_free_dev;

    dev->arch_specific = NULL;
    dev->type =
        PED_DEVICE_UNKNOWN; /* It's deprecated anyway */
    dev->open_count = 0;
    dev->read_only = 0;
    dev->external_mode = 0;
    dev->dirty = 0;
    dev->boot_dirty = 0;

    return dev;

error_free_dev:
    free(dev);
error:
    return NULL;
}

/* Ask the kernel and translators to reload the partition table.
   XXX: Will probably be replaced by some RPC to partfs when it's finished.  In
   the meantime, gnumach's glue layer will pass BLKRRPART to the Linux drivers.
   */
static int _reread_part_table(PedDevice *dev) {}

/* Free the memory associated with a PedDevice structure.  */
static void _done_device(PedDevice *dev) {}

/* Release all resources that libparted owns in DEV.  */
static void uefi_destroy(PedDevice *dev) {}

static PedDevice *uefi_new(const char *path) {}

static int uefi_is_busy(PedDevice *dev) { return 0; }

static int uefi_open(PedDevice *dev) { return 1; }

static int uefi_refresh_open(PedDevice *dev) { return 1; }

static int uefi_close(PedDevice *dev) { return 0; }

static int uefi_refresh_close(PedDevice *dev) { return 1; }

static int uefi_read(const PedDevice *dev, void *user_buffer,
                     PedSector device_start, PedSector count) {
    return 1;
}

static int uefi_write(PedDevice *dev, const void *buffer, PedSector start,
                      PedSector count) {
    return 1;
}

/* TODO: returns the number of sectors that are ok.
 */
static PedSector uefi_check(PedDevice *dev, void *buffer, PedSector start,
                            PedSector count) {
    return 0;
}

static int uefi_sync(PedDevice *dev) { return 1; }

static int probe_standard_devices() {
    // _ped_device_probe("/dev/sd0");
    // _ped_device_probe("/dev/sd1");
    // _ped_device_probe("/dev/sd2");
    // _ped_device_probe("/dev/sd3");
    // _ped_device_probe("/dev/sd4");
    // _ped_device_probe("/dev/sd5");
    //
    // _ped_device_probe("/dev/hd0");
    // _ped_device_probe("/dev/hd1");
    // _ped_device_probe("/dev/hd2");
    // _ped_device_probe("/dev/hd3");
    // _ped_device_probe("/dev/hd4");
    // _ped_device_probe("/dev/hd5");
    // _ped_device_probe("/dev/hd6");
    // _ped_device_probe("/dev/hd7");
    //
    // _ped_device_probe("/dev/wd0");
    // _ped_device_probe("/dev/wd1");
    // _ped_device_probe("/dev/wd2");
    // _ped_device_probe("/dev/wd3");
    // _ped_device_probe("/dev/wd4");
    // _ped_device_probe("/dev/wd5");
    // _ped_device_probe("/dev/wd6");
    // _ped_device_probe("/dev/wd7");

    return 1;
}

static void uefi_probe_all() { probe_standard_devices(); }

static char *uefi_partition_get_path(const PedPartition *part) { return 0; }

static int uefi_partition_is_busy(const PedPartition *part) { return 0; }

static int uefi_disk_commit(PedDisk *disk) {
    return _reread_part_table(disk->dev);
}

static PedDeviceArchOps uefi_dev_ops = {._new = uefi_new,
                                        .destroy = uefi_destroy,
                                        .is_busy = uefi_is_busy,
                                        .open = uefi_open,
                                        .refresh_open = uefi_refresh_open,
                                        .close = uefi_close,
                                        .refresh_close = uefi_refresh_close,
                                        .read = uefi_read,
                                        .write = uefi_write,
                                        .check = uefi_check,
                                        .sync = uefi_sync,
                                        .sync_fast = uefi_sync,
                                        .probe_all = uefi_probe_all};

static PedDiskArchOps uefi_disk_ops = {
    .partition_get_path = uefi_partition_get_path,
    .partition_is_busy = uefi_partition_is_busy,
    .disk_commit = uefi_disk_commit};

static const PedArchitecture ped_uefi_arch = {.dev_ops = &uefi_dev_ops,
                                              .disk_ops = &uefi_disk_ops};
