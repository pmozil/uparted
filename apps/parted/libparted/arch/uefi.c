#include <config.h>

#include <parted/debug.h>
#include <parted/parted.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../architecture.h"

/* Initialize a PedDevice using SOURCE.  The SOURCE will NOT be destroyed;
   the caller created it, it is the caller's responsilbility to free it
   after it calls ped_device_destory.  SOURCE is not registered in Parted's
   list of devices.  */
PedDevice *ped_device_new_from_store(struct store *source);

static int _device_get_sector_size(PedDevice *dev) {
    return PED_SECTOR_SIZE_DEFAULT;
}

static PedSector _device_get_length(PedDevice *dev) { return 0; }

static int _device_probe_geometry(PedDevice *dev) { return 1; }

static int init_file(PedDevice *dev) { return 0; }

static void _flush_cache(PedDevice *dev) {}

/* Initialize by allocating memory and filling in a few defaults, a
   PedDevice structure.  */
static PedDevice *_init_device(const char *path) {}

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
