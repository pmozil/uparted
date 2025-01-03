#include <config.h>

#include <parted/debug.h>
#include <parted/parted.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <xalloc.h>

#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#include "../architecture.h"

static int _device_get_sector_size(PedDevice *dev) {
    EFI_BLOCK_IO_PROTOCOL *block_io;
    EFI_HANDLE *handle = (EFI_HANDLE *)dev->arch_specific;
    EFI_STATUS status = gBS->HandleProtocol(handle, &gEfiBlockIoProtocolGuid,
                                            (VOID **)&block_io);
    if (EFI_ERROR(status)) {
        puts("Failed to open handle to device");
        return -1;
    }
    return block_io->Media->BlockSize;
}

static PedSector _device_get_length(PedDevice *dev) {
    EFI_BLOCK_IO_PROTOCOL *block_io;
    EFI_HANDLE *handle = (EFI_HANDLE *)dev->arch_specific;
    EFI_STATUS status = gBS->HandleProtocol(handle, &gEfiBlockIoProtocolGuid,
                                            (VOID **)&block_io);
    if (EFI_ERROR(status)) {
        puts("Failed to open handle to device");
        return -1;
    }
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

    dev->phys_sector_size = dev->sector_size;

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

    dev = (PedDevice *)ped_malloc(sizeof(PedDevice));
    if (!dev)
        goto error;

    dev->path = strdup(path);
    if (!dev->path)
        goto error_free_dev;

    dev->arch_specific = NULL;
    dev->type = PED_DEVICE_VIRTBLK;
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

static EFI_HANDLE *find_from_path(char const *dev_path) {
    UINTN handleCount;
    EFI_HANDLE *allHandles;
    EFI_STATUS status = EFI_SUCCESS;

    status = gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL,
                                     &handleCount, &allHandles);
    if (EFI_ERROR(status)) {
        printf("Failed to find handles. Error: %i\n\r", status);
        return NULL;
    }

    for (UINTN handleIdx = 0; handleIdx < handleCount; handleIdx++) {
        EFI_DEVICE_PATH_PROTOCOL *path =
            DevicePathFromHandle(allHandles[handleIdx]);
        while (path != NULL && !IsDevicePathEndType(path)) {
            CHAR16 const *name = ConvertDevicePathToText(path, FALSE, FALSE);
            if (!StrCmp((CHAR16 *)dev_path, name)) {
                return allHandles[handleIdx];
            }
            path = NextDevicePathNode(path);
        }
    }

    return NULL;
}

static PedDevice *uefi_new_from_handle(EFI_HANDLE *handle, const char *path) {
    PedDevice *dev;
    EFI_BLOCK_IO_PROTOCOL *block_io;
    EFI_STATUS status;
    EFI_BLOCK_IO_MEDIA *media;

    status = gBS->HandleProtocol(handle, &gEfiBlockIoProtocolGuid,
                                 (VOID **)&block_io);
    if (EFI_ERROR(status)) {
        printf("Failed to open handle to device. Status: %i\n\r", status);
        return NULL;
    }
    media = block_io->Media;

    dev = _init_device(path);
    if (dev == NULL) {
        return NULL;
    }

    dev->arch_specific = (void *)handle;
    dev->read_only = media->ReadOnly;
    _device_probe_geometry(dev);

    // dev->path = (char *)path;
    UINTN str_size = StrSize((CHAR16 *)path);
    dev->path = malloc(2 * str_size + 2);
    for (UINTN i = 0; i <= str_size; i++) {
        ((CHAR16 *)dev->path)[i] = ((CHAR16 *)path)[i];
    }
    memcpy(dev->path, path, 2 * str_size + 1);
    dev->path[2 * str_size] = L'\0';

    return dev;
}

static PedDevice *uefi_new(const char *path) {
    EFI_HANDLE *handle;
    PED_ASSERT(path != NULL);

    handle = find_from_path(path);
    if (handle == NULL)
        return NULL;

    return uefi_new_from_handle(handle, path);
}

/* Ask the kernel and translators to reload the partition table.
   XXX: Will probably be replaced by some RPC to partfs when it's finished.  In
   the meantime, gnumach's glue layer will pass BLKRRPART to the Linux drivers.
   */
static int _reread_part_table(PedDevice *dev) { return 0; }

/* Free the memory associated with a PedDevice structure.  */
static void _done_device(PedDevice *dev) {}

/* Release all resources that libparted owns in DEV.  */
static void uefi_destroy(PedDevice *dev) {}

static int uefi_is_busy(PedDevice *dev) { return 0; }

static int uefi_open(PedDevice *dev) { return 1; }

static int uefi_refresh_open(PedDevice *dev) { return 1; }

static int uefi_close(PedDevice *dev) { return 0; }

static int uefi_refresh_close(PedDevice *dev) { return 1; }

static int uefi_read(const PedDevice *dev, void *buffer, PedSector start,
                     PedSector count) {
    EFI_BLOCK_IO_PROTOCOL *block_io;
    EFI_HANDLE *handle = (EFI_HANDLE *)dev->arch_specific;
    EFI_STATUS status = gBS->HandleProtocol(handle, &gEfiBlockIoProtocolGuid,
                                            (VOID **)&block_io);
    if (EFI_ERROR(status)) {
        puts("Failed to open handle to device");
        return 0;
    }
    status =
        block_io->ReadBlocks(block_io, block_io->Media->MediaId, start,
                             (UINTN)count * block_io->Media->BlockSize, buffer);
    if (EFI_ERROR(status) || buffer == NULL) {
        puts("Failed to read from device");
    }
    return 1;
}

static int uefi_write(PedDevice *dev, const void *buffer, PedSector start,
                      PedSector count) {
    EFI_BLOCK_IO_PROTOCOL *block_io;
    EFI_HANDLE *handle = (EFI_HANDLE *)dev->arch_specific;
    EFI_STATUS status = gBS->HandleProtocol(handle, &gEfiBlockIoProtocolGuid,
                                            (VOID **)&block_io);
    if (EFI_ERROR(status)) {
        puts("Failed to open handle to device");
        return 0;
    }
    status = block_io->WriteBlocks(block_io, block_io->Media->MediaId, start,
                                   (UINTN)count * block_io->Media->BlockSize,
                                   (void *)buffer);
    if (EFI_ERROR(status)) {
        puts("Failed to read from device");
    }
    return 1;
}

/* TODO: returns the number of sectors that are ok.
 */
static PedSector uefi_check(PedDevice *dev, void *buffer, PedSector start,
                            PedSector count) {
    return 0;
}

static int uefi_sync(PedDevice *dev) {
    EFI_BLOCK_IO_PROTOCOL *block_io;
    EFI_HANDLE *handle = (EFI_HANDLE *)dev->arch_specific;
    EFI_STATUS status = gBS->HandleProtocol(handle, &gEfiBlockIoProtocolGuid,
                                            (VOID **)&block_io);
    if (EFI_ERROR(status)) {
        puts("Failed to open handle to device");
        return 0;
    }
    status = block_io->FlushBlocks(block_io);
    if (EFI_ERROR(status)) {
        puts("Failed to read from device");
    }
    return 1;
}

static void uefi_probe_all() {
    EFI_STATUS status = EFI_SUCCESS;

    UINTN handleCount;
    EFI_HANDLE *allHandles;
    status = gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL,
                                     &handleCount, &allHandles);

    if (EFI_ERROR(status))
        return;

    for (UINTN handleIdx = 0; handleIdx < handleCount; handleIdx++) {
        EFI_DEVICE_PATH_PROTOCOL *path =
            DevicePathFromHandle(allHandles[handleIdx]);
        while (path != NULL && !IsDevicePathEndType(path)) {
            EFI_BLOCK_IO_PROTOCOL *blockIo;
            // Open Block IO protocol on the current handle
            status = gBS->HandleProtocol(allHandles[handleIdx],
                                         &gEfiBlockIoProtocolGuid,
                                         (VOID **)&blockIo);
            if (EFI_ERROR(status)) {
                goto next_path;
            }

            // Check if the handle is a physical disk
            if (blockIo->Media->LogicalPartition) {
                // Skip logical partitions, as we want disks only
                goto next_path;
            }

            if (!blockIo->Media->MediaPresent) {
                // Skip if no media is present
                goto next_path;
            }
            _ped_device_probe(
                (char *)ConvertDevicePathToText(path, FALSE, FALSE));
            break;
        next_path:
            path = NextDevicePathNode(path);
        }
    }
}

static char *uefi_partition_get_path(const PedPartition *part) {
    UINTN str_len = StrSize((CHAR16 *)part->disk->dev->path);
    CHAR16 *buf = (CHAR16 *)malloc(str_len * 2 + 32);
    CHAR16 *fmt = L"%sp%d";

    UnicodeSPrint(buf, str_len * 2 + 32, fmt, part->disk->dev->path, part->num);

    return (char *)buf;
}

static int uefi_partition_is_busy(const PedPartition *part) {
    return 0; // assume always can edit a partition - don't believe UEFI has any
              // notion of not being available
}

static int uefi_disk_commit(PedDisk *disk) {
    return _reread_part_table(disk->dev);
}

PedDeviceArchOps uefi_dev_ops = {
    ._new = uefi_new,
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
    .probe_all = uefi_probe_all,
};

PedDiskArchOps uefi_disk_ops = {
    .partition_get_path = uefi_partition_get_path,
    .partition_is_busy = uefi_partition_is_busy,
    .disk_commit = uefi_disk_commit,
};

PedArchitecture ped_uefi_arch = {
    .dev_ops = &uefi_dev_ops,
    .disk_ops = &uefi_disk_ops,
};
